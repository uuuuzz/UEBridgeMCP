// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/EditLevelActorTool.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "FileHelpers.h"

namespace EditLevelActorToolPrivate
{
	void ApplyTransformDescriptor(const TSharedPtr<FJsonObject>& TransformObject, FTransform& InOutTransform)
	{
		if (!TransformObject.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
		if (TransformObject->TryGetArrayField(TEXT("location"), LocationArray) && LocationArray && LocationArray->Num() >= 3)
		{
			InOutTransform.SetLocation(FVector((*LocationArray)[0]->AsNumber(), (*LocationArray)[1]->AsNumber(), (*LocationArray)[2]->AsNumber()));
		}

		const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
		if (TransformObject->TryGetArrayField(TEXT("rotation"), RotationArray) && RotationArray && RotationArray->Num() >= 3)
		{
			InOutTransform.SetRotation(FRotator((*RotationArray)[0]->AsNumber(), (*RotationArray)[1]->AsNumber(), (*RotationArray)[2]->AsNumber()).Quaternion());
		}

		const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;
		if (TransformObject->TryGetArrayField(TEXT("scale"), ScaleArray) && ScaleArray && ScaleArray->Num() >= 3)
		{
			InOutTransform.SetScale3D(FVector((*ScaleArray)[0]->AsNumber(), (*ScaleArray)[1]->AsNumber(), (*ScaleArray)[2]->AsNumber()));
		}
	}

	bool ApplyPropertyOverrides(UObject* Object, const TSharedPtr<FJsonObject>& PropertiesObject, FString& OutError)
	{
		if (!Object || !PropertiesObject.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
		{
			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString PropertyError;

			if (!FMcpAssetModifier::FindPropertyByPath(Object, Pair.Key, Property, Container, PropertyError))
			{
				OutError = FString::Printf(TEXT("Failed to find property '%s': %s"), *Pair.Key, *PropertyError);
				return false;
			}

			if (!FMcpAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, PropertyError))
			{
				OutError = FString::Printf(TEXT("Failed to set property '%s': %s"), *Pair.Key, *PropertyError);
				return false;
			}
		}

		return true;
	}
}

FString UEditLevelActorTool::GetToolDescription() const
{
	return TEXT("Comprehensive level actor editing: spawn, delete, duplicate, set_transform, attach, detach, "
		"set_folder, set_label, add_component, remove_component. Batched via 'actions' array.");
}

TMap<FString, FMcpSchemaProperty> UEditLevelActorTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("Target world: 'editor', 'pie', or 'auto'"),
		{TEXT("editor"), TEXT("pie"), TEXT("auto")}));

	TSharedPtr<FMcpSchemaProperty> TransformSchema = MakeShared<FMcpSchemaProperty>();
	TransformSchema->Type = TEXT("object");
	TransformSchema->Description = TEXT("Transform descriptor with location, rotation, and scale arrays");
	TransformSchema->Properties.Add(TEXT("location"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Location [X,Y,Z]"), TEXT("number"))));
	TransformSchema->Properties.Add(TEXT("rotation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Rotation [Pitch,Yaw,Roll]"), TEXT("number"))));
	TransformSchema->Properties.Add(TEXT("scale"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Scale [X,Y,Z]"), TEXT("number"))));

	TSharedPtr<FJsonObject> PropertiesRawSchema = MakeShareable(new FJsonObject);
	PropertiesRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	PropertiesRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> PropertiesSchema = MakeShared<FMcpSchemaProperty>();
	PropertiesSchema->Description = TEXT("Property overrides applied to the spawned actor");
	PropertiesSchema->RawSchema = PropertiesRawSchema;

	TSharedPtr<FMcpSchemaProperty> ActionItemSchema = MakeShared<FMcpSchemaProperty>();
	ActionItemSchema->Type = TEXT("object");
	ActionItemSchema->Description = TEXT("Actor edit action");
	ActionItemSchema->NestedRequired = {TEXT("action")};
	ActionItemSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Actor edit action"),
		{TEXT("spawn_actor"), TEXT("delete_actor"), TEXT("duplicate_actor"), TEXT("set_transform"), TEXT("attach_actor"), TEXT("detach_actor"), TEXT("set_folder"), TEXT("set_label"), TEXT("add_component"), TEXT("remove_component")},
		true)));
	ActionItemSchema->Properties.Add(TEXT("actor_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor class path for spawn_actor"))));
	ActionItemSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor name or label"))));
	ActionItemSchema->Properties.Add(TEXT("actor_label"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor label for spawn_actor or set_label"))));
	ActionItemSchema->Properties.Add(TEXT("parent_actor"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Parent actor for attach_actor"))));
	ActionItemSchema->Properties.Add(TEXT("transform"), TransformSchema);
	ActionItemSchema->Properties.Add(TEXT("folder_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("World Outliner folder path"))));
	ActionItemSchema->Properties.Add(TEXT("tags"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Actor tags"), TEXT("string"))));
	ActionItemSchema->Properties.Add(TEXT("properties"), PropertiesSchema);
	ActionItemSchema->Properties.Add(TEXT("select_after_spawn"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Select the actor after spawn in the editor world"))));
	ActionItemSchema->Properties.Add(TEXT("new_actor_label"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New label for duplicate_actor"))));
	ActionItemSchema->Properties.Add(TEXT("offset"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Duplicate offset [X,Y,Z]"), TEXT("number"))));
	ActionItemSchema->Properties.Add(TEXT("socket_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional attachment socket name"))));
	ActionItemSchema->Properties.Add(TEXT("keep_world_transform"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Keep world transform when attaching or detaching (default true)"))));
	ActionItemSchema->Properties.Add(TEXT("component_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component class path for add_component"))));
	ActionItemSchema->Properties.Add(TEXT("component_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component name for add_component or remove_component"))));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Array of actor edit actions with nested transform and properties descriptors.");
	ActionsSchema.bRequired = true;
	ActionsSchema.Items = ActionItemSchema;
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after edits. Ignored for PIE worlds.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transaction label")));

	return Schema;
}

TArray<FString> UEditLevelActorTool::GetRequiredParams() const
{
	return {TEXT("actions")};
}

FMcpToolResult UEditLevelActorTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Level Actor"));

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	for (int32 i = 0; i < ActionsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& ActionValue = (*ActionsArray)[i];
		const TSharedPtr<FJsonObject>* ActionObj = nullptr;
		if (!ActionValue.IsValid() || !ActionValue->TryGetObject(ActionObj) || !(*ActionObj).IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActionResult = MakeShareable(new FJsonObject);
		FString ActionError;

		bool bSuccess = false;
		if (!bDryRun)
		{
			bSuccess = ExecuteAction(World, *ActionObj, i, ActionResult, ActionError);
		}
		else
		{
			FString ActionName;
			(*ActionObj)->TryGetStringField(TEXT("action"), ActionName);
			ActionResult->SetStringField(TEXT("action"), ActionName);
			bSuccess = true;
		}

		ActionResult->SetNumberField(TEXT("index"), i);
		ActionResult->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			ActionResult->SetStringField(TEXT("error"), ActionError);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
			if (bRollbackOnError && !bDryRun)
			{
				Transaction.Reset();
				TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
				PartialResult->SetArrayField(TEXT("results"), ResultsArray);
				PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), ActionError, nullptr, PartialResult);
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
	}

	if (!bDryRun && bSave)
	{
		if (World->WorldType == EWorldType::Editor && World->PersistentLevel)
		{
			if (!FEditorFileUtils::SaveLevel(World->PersistentLevel))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_LEVEL_SAVE_FAILED"), TEXT("Failed to save the current editor level"));
			}
		}
		else
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("save=true was ignored because the resolved world is not a saveable editor world"))));
		}
	}

	if (World->PersistentLevel)
	{
		const FString LevelPath = World->PersistentLevel->GetPackage()->GetPathName();
		if (!LevelPath.IsEmpty())
		{
			ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(LevelPath)));
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("edit-level-actor"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

bool UEditLevelActorTool::ExecuteAction(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Action,
	int32 Index,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActionName;
	if (!Action->TryGetStringField(TEXT("action"), ActionName))
	{
		OutError = TEXT("Missing 'action' field");
		return false;
	}
	OutResult->SetStringField(TEXT("action"), ActionName);

	if (ActionName == TEXT("spawn_actor")) { return SpawnActor(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("delete_actor")) { return DeleteActor(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("duplicate_actor")) { return DuplicateActor(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("set_transform")) { return SetTransform(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("attach_actor")) { return AttachActor(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("detach_actor")) { return DetachActor(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("set_folder")) { return SetFolder(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("set_label")) { return SetLabel(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("add_component")) { return AddActorComponent(World, Action, OutResult, OutError); }
	if (ActionName == TEXT("remove_component")) { return RemoveActorComponent(World, Action, OutResult, OutError); }

	OutError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
	return false;
}

bool UEditLevelActorTool::SpawnActor(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorClassPath;
	if (!Action->TryGetStringField(TEXT("actor_class"), ActorClassPath))
	{
		OutError = TEXT("'actor_class' is required for spawn_actor");
		return false;
	}

	UClass* ActorClass = FindObject<UClass>(nullptr, *ActorClassPath);
	if (!ActorClass)
	{
		ActorClass = LoadObject<UClass>(nullptr, *ActorClassPath);
	}
	// 尝试作为蓝图加载
	if (!ActorClass)
	{
		FString BPPath = ActorClassPath;
		if (!BPPath.EndsWith(TEXT("_C")))
		{
			BPPath += TEXT("_C");
		}
		ActorClass = LoadObject<UClass>(nullptr, *BPPath);
	}
	if (!ActorClass)
	{
		OutError = FString::Printf(TEXT("Actor class not found: '%s'"), *ActorClassPath);
		return false;
	}

	FTransform SpawnTransform = FTransform::Identity;
	const TSharedPtr<FJsonObject>* TransformObj = nullptr;
	if (Action->TryGetObjectField(TEXT("transform"), TransformObj) && TransformObj && (*TransformObj).IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
		if ((*TransformObj)->TryGetArrayField(TEXT("location"), LocArr) && LocArr && LocArr->Num() >= 3)
		{
			SpawnTransform.SetLocation(FVector((*LocArr)[0]->AsNumber(), (*LocArr)[1]->AsNumber(), (*LocArr)[2]->AsNumber()));
		}
		const TArray<TSharedPtr<FJsonValue>>* RotArr = nullptr;
		if ((*TransformObj)->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr && RotArr->Num() >= 3)
		{
			SpawnTransform.SetRotation(FRotator((*RotArr)[0]->AsNumber(), (*RotArr)[1]->AsNumber(), (*RotArr)[2]->AsNumber()).Quaternion());
		}
		const TArray<TSharedPtr<FJsonValue>>* ScaleArr = nullptr;
		if ((*TransformObj)->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr && ScaleArr->Num() >= 3)
		{
			SpawnTransform.SetScale3D(FVector((*ScaleArr)[0]->AsNumber(), (*ScaleArr)[1]->AsNumber(), (*ScaleArr)[2]->AsNumber()));
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FString ActorObjectName;
	if (Action->TryGetStringField(TEXT("actor_name"), ActorObjectName) && !ActorObjectName.IsEmpty())
	{
		SpawnParams.Name = FName(*ActorObjectName);
	}

	FString ActorLabel;
	Action->TryGetStringField(TEXT("actor_label"), ActorLabel);

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);

	if (!NewActor)
	{
		OutError = TEXT("Failed to spawn actor");
		return false;
	}

	if (!ActorLabel.IsEmpty())
	{
		NewActor->SetActorLabel(ActorLabel);
	}

	// 设置文件夹
	FString FolderPath;
	if (Action->TryGetStringField(TEXT("folder_path"), FolderPath))
	{
		NewActor->SetFolderPath(FName(*FolderPath));
	}

	// 设置 Tags
	const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
	if (Action->TryGetArrayField(TEXT("tags"), TagsArr) && TagsArr)
	{
		for (const TSharedPtr<FJsonValue>& TagVal : *TagsArr)
		{
			NewActor->Tags.Add(FName(*TagVal->AsString()));
		}
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	if (Action->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && (*PropertiesObject).IsValid())
	{
		if (!EditLevelActorToolPrivate::ApplyPropertyOverrides(NewActor, *PropertiesObject, OutError))
		{
			NewActor->Destroy();
			return false;
		}
	}

	const bool bSelectAfterSpawn = GetBoolArgOrDefault(Action, TEXT("select_after_spawn"), false);
	if (bSelectAfterSpawn && GEditor && World->WorldType == EWorldType::Editor)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(NewActor, true, true, true);
	}

	OutResult->SetStringField(TEXT("actor_name"), NewActor->GetActorNameOrLabel());
	return true;
}

bool UEditLevelActorTool::DeleteActor(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("'actor_name' is required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName);
		return false;
	}

	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	World->DestroyActor(Actor);
	return true;
}

bool UEditLevelActorTool::DuplicateActor(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("'actor_name' is required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName);
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Template = Actor;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform DupTransform = Actor->GetActorTransform();

	// 可选偏移
	const TArray<TSharedPtr<FJsonValue>>* OffsetArr = nullptr;
	if (Action->TryGetArrayField(TEXT("offset"), OffsetArr) && OffsetArr && OffsetArr->Num() >= 3)
	{
		FVector Offset((*OffsetArr)[0]->AsNumber(), (*OffsetArr)[1]->AsNumber(), (*OffsetArr)[2]->AsNumber());
		DupTransform.SetLocation(DupTransform.GetLocation() + Offset);
	}

	AActor* NewActor = World->SpawnActor<AActor>(Actor->GetClass(), DupTransform, SpawnParams);
	if (!NewActor)
	{
		OutError = TEXT("Failed to duplicate actor");
		return false;
	}

	FString NewLabel;
	if (Action->TryGetStringField(TEXT("new_actor_label"), NewLabel))
	{
		NewActor->SetActorLabel(NewLabel);
	}

	OutResult->SetStringField(TEXT("actor_name"), NewActor->GetActorNameOrLabel());
	return true;
}

bool UEditLevelActorTool::SetTransform(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActorName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("'actor_name' is required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName);
		return false;
	}

	OutResult->SetStringField(TEXT("actor_name"), ActorName);

	const TSharedPtr<FJsonObject>* TransformObj = nullptr;
	if (Action->TryGetObjectField(TEXT("transform"), TransformObj) && TransformObj && (*TransformObj).IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
		if ((*TransformObj)->TryGetArrayField(TEXT("location"), LocArr) && LocArr && LocArr->Num() >= 3)
		{
			Actor->SetActorLocation(FVector((*LocArr)[0]->AsNumber(), (*LocArr)[1]->AsNumber(), (*LocArr)[2]->AsNumber()));
		}
		const TArray<TSharedPtr<FJsonValue>>* RotArr = nullptr;
		if ((*TransformObj)->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr && RotArr->Num() >= 3)
		{
			Actor->SetActorRotation(FRotator((*RotArr)[0]->AsNumber(), (*RotArr)[1]->AsNumber(), (*RotArr)[2]->AsNumber()));
		}
		const TArray<TSharedPtr<FJsonValue>>* ScaleArr = nullptr;
		if ((*TransformObj)->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr && ScaleArr->Num() >= 3)
		{
			Actor->SetActorScale3D(FVector((*ScaleArr)[0]->AsNumber(), (*ScaleArr)[1]->AsNumber(), (*ScaleArr)[2]->AsNumber()));
		}
	}

	return true;
}

bool UEditLevelActorTool::AttachActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActorName, ParentName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName) || !Action->TryGetStringField(TEXT("parent_actor"), ParentName))
	{
		OutError = TEXT("'actor_name' and 'parent_actor' are required");
		return false;
	}

	AActor* Child = FMcpAssetModifier::FindActorByName(World, ActorName);
	AActor* Parent = FMcpAssetModifier::FindActorByName(World, ParentName);
	if (!Child) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName); return false; }
	if (!Parent) { OutError = FString::Printf(TEXT("Parent actor '%s' not found"), *ParentName); return false; }

	const bool bKeepWorldTransform = GetBoolArgOrDefault(Action, TEXT("keep_world_transform"), true);
	const FString SocketName = GetStringArgOrDefault(Action, TEXT("socket_name"));
	const FAttachmentTransformRules Rules = bKeepWorldTransform
		? FAttachmentTransformRules::KeepWorldTransform
		: FAttachmentTransformRules::KeepRelativeTransform;
	Child->AttachToActor(Parent, Rules, SocketName.IsEmpty() ? NAME_None : FName(*SocketName));

	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	OutResult->SetStringField(TEXT("parent_actor"), ParentName);
	if (!SocketName.IsEmpty())
	{
		OutResult->SetStringField(TEXT("socket_name"), SocketName);
	}
	return true;
}

bool UEditLevelActorTool::DetachActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActorName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName))
	{
		OutError = TEXT("'actor_name' is required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName); return false; }

	const bool bKeepWorldTransform = GetBoolArgOrDefault(Action, TEXT("keep_world_transform"), true);
	const FDetachmentTransformRules Rules = bKeepWorldTransform
		? FDetachmentTransformRules(EDetachmentRule::KeepWorld, false)
		: FDetachmentTransformRules(EDetachmentRule::KeepRelative, false);
	Actor->DetachFromActor(Rules);

	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	return true;
}

bool UEditLevelActorTool::SetFolder(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActorName, FolderPath;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName) || !Action->TryGetStringField(TEXT("folder_path"), FolderPath))
	{
		OutError = TEXT("'actor_name' and 'folder_path' are required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName); return false; }

	Actor->SetFolderPath(FName(*FolderPath));
	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	return true;
}

bool UEditLevelActorTool::SetLabel(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActorName, ActorLabel;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName) || !Action->TryGetStringField(TEXT("actor_label"), ActorLabel))
	{
		OutError = TEXT("'actor_name' and 'actor_label' are required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName); return false; }

	Actor->SetActorLabel(ActorLabel);
	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	return true;
}

bool UEditLevelActorTool::AddActorComponent(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActorName, CompClass, CompName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName) ||
		!Action->TryGetStringField(TEXT("component_class"), CompClass) ||
		!Action->TryGetStringField(TEXT("component_name"), CompName))
	{
		OutError = TEXT("'actor_name', 'component_class', and 'component_name' are required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName); return false; }

	UClass* ComponentClass = FindObject<UClass>(nullptr, *CompClass);
	if (!ComponentClass) { ComponentClass = LoadObject<UClass>(nullptr, *CompClass); }
	if (!ComponentClass) { OutError = FString::Printf(TEXT("Component class '%s' not found"), *CompClass); return false; }

	UActorComponent* NewComp = NewObject<UActorComponent>(Actor, ComponentClass, FName(*CompName));
	if (!NewComp) { OutError = TEXT("Failed to create component"); return false; }

	Actor->AddInstanceComponent(NewComp);
	NewComp->RegisterComponent();

	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	OutResult->SetStringField(TEXT("component_name"), CompName);
	return true;
}

bool UEditLevelActorTool::RemoveActorComponent(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActorName, CompName;
	if (!Action->TryGetStringField(TEXT("actor_name"), ActorName) ||
		!Action->TryGetStringField(TEXT("component_name"), CompName))
	{
		OutError = TEXT("'actor_name' and 'component_name' are required");
		return false;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorName); return false; }

	UActorComponent* Comp = FMcpAssetModifier::FindComponentByName(Actor, CompName);
	if (!Comp) { OutError = FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *CompName, *ActorName); return false; }

	Comp->DestroyComponent();
	OutResult->SetStringField(TEXT("actor_name"), ActorName);
	OutResult->SetStringField(TEXT("component_name"), CompName);
	return true;
}