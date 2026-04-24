// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/EditLevelBatchTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "UEBridgeMCP.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Serialization/JsonSerializer.h"

namespace EditLevelBatchToolPrivate
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

	bool GetBoolFieldOrDefault(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool DefaultValue)
	{
		bool bValue = DefaultValue;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	FString GetStringFieldOrDefault(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const FString& DefaultValue = FString())
	{
		FString Value = DefaultValue;
		if (Object.IsValid())
		{
			Object->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool TryGetOperationArray(
		const TSharedPtr<FJsonObject>& Arguments,
		const TArray<TSharedPtr<FJsonValue>>*& OutOperations,
		FString& OutErrorCode,
		FString& OutErrorMessage)
	{
		const bool bHasOperations = Arguments->HasField(TEXT("operations"));
		const bool bHasActions = Arguments->HasField(TEXT("actions"));
		if (bHasOperations && bHasActions)
		{
			OutErrorCode = TEXT("UEBMCP_INVALID_ARG");
			OutErrorMessage = TEXT("Provide either 'operations' or legacy 'actions', not both");
			return false;
		}

		if (bHasOperations)
		{
			if (!Arguments->TryGetArrayField(TEXT("operations"), OutOperations) || !OutOperations || OutOperations->Num() == 0)
			{
				OutErrorCode = TEXT("UEBMCP_MISSING_REQUIRED_FIELD");
				OutErrorMessage = TEXT("'operations' array is required");
				return false;
			}
			return true;
		}

		if (bHasActions)
		{
			if (!Arguments->TryGetArrayField(TEXT("actions"), OutOperations) || !OutOperations || OutOperations->Num() == 0)
			{
				OutErrorCode = TEXT("UEBMCP_MISSING_REQUIRED_FIELD");
				OutErrorMessage = TEXT("'actions' array is required");
				return false;
			}
			return true;
		}

		OutErrorCode = TEXT("UEBMCP_MISSING_REQUIRED_FIELD");
		OutErrorMessage = TEXT("'operations' array is required");
		return false;
	}

	bool ResolveTargetActor(
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		UWorld*& InOutWorld,
		AActor*& OutActor,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		TSharedPtr<FJsonObject>& OutDetails)
	{
		UWorld* ResolvedWorld = nullptr;
		OutActor = LevelActorToolUtils::ResolveActorReference(
			Operation,
			WorldType,
			TEXT("actor_name"),
			TEXT("actor_handle"),
			Context,
			ResolvedWorld,
			OutErrorCode,
			OutErrorMessage,
			OutDetails,
			true);
		if (!OutActor)
		{
			return false;
		}

		if (!InOutWorld)
		{
			InOutWorld = ResolvedWorld;
		}
		else if (ResolvedWorld && InOutWorld->GetPathName() != ResolvedWorld->GetPathName())
		{
			OutErrorCode = TEXT("UEBMCP_WORLD_MISMATCH");
			OutErrorMessage = TEXT("All level batch operations must resolve to the same world");
			OutDetails = MakeShareable(new FJsonObject);
			OutDetails->SetStringField(TEXT("expected_world"), InOutWorld->GetPathName());
			OutDetails->SetStringField(TEXT("actual_world"), ResolvedWorld->GetPathName());
			return false;
		}

		return true;
	}

	bool ResolveParentActor(
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		UWorld*& InOutWorld,
		AActor*& OutParentActor,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		TSharedPtr<FJsonObject>& OutDetails)
	{
		UWorld* ResolvedWorld = nullptr;
		OutParentActor = LevelActorToolUtils::ResolveActorReference(
			Operation,
			WorldType,
			TEXT("parent_actor"),
			TEXT("parent_actor_handle"),
			Context,
			ResolvedWorld,
			OutErrorCode,
			OutErrorMessage,
			OutDetails,
			true);
		if (!OutParentActor)
		{
			return false;
		}

		if (!InOutWorld)
		{
			InOutWorld = ResolvedWorld;
		}
		else if (ResolvedWorld && InOutWorld->GetPathName() != ResolvedWorld->GetPathName())
		{
			OutErrorCode = TEXT("UEBMCP_WORLD_MISMATCH");
			OutErrorMessage = TEXT("All level batch operations must resolve to the same world");
			OutDetails = MakeShareable(new FJsonObject);
			OutDetails->SetStringField(TEXT("expected_world"), InOutWorld->GetPathName());
			OutDetails->SetStringField(TEXT("actual_world"), ResolvedWorld->GetPathName());
			return false;
		}

		return true;
	}

	bool ResolveWorldForOperation(const FString& WorldType, UWorld*& InOutWorld, FString& OutErrorCode, FString& OutErrorMessage)
	{
		if (InOutWorld)
		{
			return true;
		}

		InOutWorld = FMcpAssetModifier::ResolveWorld(WorldType);
		if (!InOutWorld)
		{
			OutErrorCode = TEXT("UEBMCP_WORLD_NOT_AVAILABLE");
			OutErrorMessage = TEXT("No world available");
			return false;
		}
		return true;
	}

	bool ResolveActorClass(const FString& ClassName, UClass*& OutClass, FString& OutError)
	{
		FString ResolveError;
		UClass* ResolvedClass = FMcpPropertySerializer::ResolveClass(ClassName, ResolveError);
		if (!ResolvedClass)
		{
			OutError = ResolveError;
			return false;
		}
		if (!ResolvedClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Class '%s' is not an AActor subclass"), *ClassName);
			return false;
		}

		OutClass = ResolvedClass;
		return true;
	}

	bool ResolveComponentClass(const FString& ClassName, UClass*& OutClass, FString& OutError)
	{
		FString ResolveError;
		UClass* ResolvedClass = FMcpPropertySerializer::ResolveClass(ClassName, ResolveError);
		if (!ResolvedClass)
		{
			OutError = ResolveError;
			return false;
		}
		if (!ResolvedClass->IsChildOf(UActorComponent::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Class '%s' is not a UActorComponent subclass"), *ClassName);
			return false;
		}

		OutClass = ResolvedClass;
		return true;
	}

	void SetChangedFlag(TSharedPtr<FJsonObject>& OutResult, bool bChanged)
	{
		if (OutResult.IsValid())
		{
			OutResult->SetBoolField(TEXT("changed"), bChanged);
		}
	}

	bool SpawnActor(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		FString ErrorCode;
		if (!ResolveWorldForOperation(WorldType, InOutWorld, ErrorCode, OutError))
		{
			return false;
		}

		FString ActorClassName;
		if (!Operation->TryGetStringField(TEXT("actor_class"), ActorClassName))
		{
			OutError = TEXT("'actor_class' is required for spawn_actor");
			return false;
		}

		UClass* ActorClass = nullptr;
		if (!ResolveActorClass(ActorClassName, ActorClass, OutError))
		{
			return false;
		}

		FTransform SpawnTransform = FTransform::Identity;
		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		if (Operation->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject && (*TransformObject).IsValid())
		{
			ApplyTransformDescriptor(*TransformObject, SpawnTransform);
		}

		FString ActorObjectName;
		Operation->TryGetStringField(TEXT("actor_name"), ActorObjectName);
		FString ActorLabel;
		Operation->TryGetStringField(TEXT("actor_label"), ActorLabel);

		OutResult->SetStringField(TEXT("actor_class"), ActorClass->GetPathName());
		if (!ActorObjectName.IsEmpty())
		{
			OutResult->SetStringField(TEXT("actor_name"), ActorObjectName);
		}
		if (!ActorLabel.IsEmpty())
		{
			OutResult->SetStringField(TEXT("actor_label"), ActorLabel);
		}
		SetChangedFlag(OutResult, true);

		if (bDryRun)
		{
			return true;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (!ActorObjectName.IsEmpty())
		{
			SpawnParams.Name = FName(*ActorObjectName);
		}

		AActor* NewActor = InOutWorld->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
		if (!NewActor)
		{
			OutError = TEXT("Failed to spawn actor");
			return false;
		}

		NewActor->Modify();

		if (!ActorLabel.IsEmpty())
		{
			NewActor->SetActorLabel(ActorLabel);
		}

		FString FolderPath;
		if (Operation->TryGetStringField(TEXT("folder_path"), FolderPath))
		{
			NewActor->SetFolderPath(FName(*FolderPath));
		}

		const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
		if (Operation->TryGetArrayField(TEXT("tags"), TagsArray) && TagsArray)
		{
			NewActor->Tags.Reset();
			for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
			{
				NewActor->Tags.Add(FName(*TagValue->AsString()));
			}
		}

		const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
		if (Operation->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && (*PropertiesObject).IsValid())
		{
			if (!ApplyPropertyOverrides(NewActor, *PropertiesObject, OutError))
			{
				NewActor->Destroy();
				return false;
			}
		}

		const bool bSelectAfterSpawn = GetBoolFieldOrDefault(Operation, TEXT("select_after_spawn"), false);
		if (bSelectAfterSpawn && GEditor && InOutWorld->WorldType == EWorldType::Editor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(NewActor, true, true, true);
		}

		OutResult->SetStringField(TEXT("actor_name"), NewActor->GetActorNameOrLabel());
		return true;
	}

	bool DeleteActor(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		SetChangedFlag(OutResult, true);
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		InOutWorld->DestroyActor(Actor);
		return true;
	}

	bool DuplicateActor(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* SourceActor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, SourceActor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		FTransform DuplicateTransform = SourceActor->GetActorTransform();
		const TArray<TSharedPtr<FJsonValue>>* OffsetArray = nullptr;
		if (Operation->TryGetArrayField(TEXT("offset"), OffsetArray) && OffsetArray && OffsetArray->Num() >= 3)
		{
			const FVector Offset((*OffsetArray)[0]->AsNumber(), (*OffsetArray)[1]->AsNumber(), (*OffsetArray)[2]->AsNumber());
			DuplicateTransform.SetLocation(DuplicateTransform.GetLocation() + Offset);
		}

		FString NewLabel;
		Operation->TryGetStringField(TEXT("new_actor_label"), NewLabel);
		OutResult->SetStringField(TEXT("source_actor"), SourceActor->GetActorNameOrLabel());
		if (!NewLabel.IsEmpty())
		{
			OutResult->SetStringField(TEXT("actor_label"), NewLabel);
		}
		SetChangedFlag(OutResult, true);

		if (bDryRun)
		{
			return true;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Template = SourceActor;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* DuplicatedActor = InOutWorld->SpawnActor<AActor>(SourceActor->GetClass(), DuplicateTransform, SpawnParams);
		if (!DuplicatedActor)
		{
			OutError = TEXT("Failed to duplicate actor");
			return false;
		}

		DuplicatedActor->Modify();
		if (!NewLabel.IsEmpty())
		{
			DuplicatedActor->SetActorLabel(NewLabel);
		}

		OutResult->SetStringField(TEXT("actor_name"), DuplicatedActor->GetActorNameOrLabel());
		return true;
	}

	bool SetTransform(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		if (!Operation->TryGetObjectField(TEXT("transform"), TransformObject) || !TransformObject || !(*TransformObject).IsValid())
		{
			OutError = TEXT("'transform' object is required for set_transform");
			return false;
		}

		const FTransform BeforeTransform = Actor->GetActorTransform();
		FTransform AfterTransform = BeforeTransform;
		ApplyTransformDescriptor(*TransformObject, AfterTransform);

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetBoolField(TEXT("changed"), !BeforeTransform.Equals(AfterTransform));
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		Actor->SetActorTransform(AfterTransform);
		return true;
	}

	bool AttachLikeOperation(
		const TCHAR* ParentActorNameField,
		const TCHAR* ParentActorHandleField,
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		TSharedPtr<FJsonObject> ParentArgs = MakeShareable(new FJsonObject());
		ParentArgs->Values = Operation->Values;
		AActor* ParentActor = nullptr;
		UWorld* ParentWorld = InOutWorld;
		ParentActor = LevelActorToolUtils::ResolveActorReference(
			ParentArgs,
			WorldType,
			ParentActorNameField,
			ParentActorHandleField,
			Context,
			ParentWorld,
			ErrorCode,
			OutError,
			ErrorDetails,
			true);
		if (!ParentActor)
		{
			return false;
		}
		if (ParentWorld && InOutWorld && ParentWorld->GetPathName() != InOutWorld->GetPathName())
		{
			OutError = TEXT("Actor and parent actor must resolve to the same world");
			return false;
		}

		const bool bKeepWorldTransform = GetBoolFieldOrDefault(Operation, TEXT("keep_world_transform"), true);
		const FString SocketName = GetStringFieldOrDefault(Operation, TEXT("socket_name"));
		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetStringField(TEXT("parent_actor"), ParentActor->GetActorNameOrLabel());
		SetChangedFlag(OutResult, true);
		if (!SocketName.IsEmpty())
		{
			OutResult->SetStringField(TEXT("socket_name"), SocketName);
		}

		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		const FAttachmentTransformRules Rules = bKeepWorldTransform
			? FAttachmentTransformRules::KeepWorldTransform
			: FAttachmentTransformRules::KeepRelativeTransform;
		Actor->AttachToActor(ParentActor, Rules, SocketName.IsEmpty() ? NAME_None : FName(*SocketName));
		return true;
	}

	bool DetachActor(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		const bool bKeepWorldTransform = GetBoolFieldOrDefault(Operation, TEXT("keep_world_transform"), true);
		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		SetChangedFlag(OutResult, Actor->GetAttachParentActor() != nullptr);
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		const FDetachmentTransformRules Rules = bKeepWorldTransform
			? FDetachmentTransformRules::KeepWorldTransform
			: FDetachmentTransformRules::KeepRelativeTransform;
		Actor->DetachFromActor(Rules);
		return true;
	}

	bool SetFolder(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		FString FolderPath;
		if (!Operation->TryGetStringField(TEXT("folder_path"), FolderPath))
		{
			OutError = TEXT("'folder_path' is required for set_folder");
			return false;
		}

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetStringField(TEXT("folder_path"), FolderPath);
		SetChangedFlag(OutResult, Actor->GetFolderPath().ToString() != FolderPath);
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		Actor->SetFolderPath(FName(*FolderPath));
		return true;
	}

	bool SetLabel(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		FString ActorLabel;
		if (!Operation->TryGetStringField(TEXT("actor_label"), ActorLabel))
		{
			OutError = TEXT("'actor_label' is required for set_label");
			return false;
		}

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetStringField(TEXT("actor_label"), ActorLabel);
		SetChangedFlag(OutResult, Actor->GetActorNameOrLabel() != ActorLabel);
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		Actor->SetActorLabel(ActorLabel);
		return true;
	}

	bool SetTags(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
		if (!Operation->TryGetArrayField(TEXT("tags"), TagsArray) || !TagsArray)
		{
			OutError = TEXT("'tags' array is required for set_tags");
			return false;
		}

		TArray<FName> NewTags;
		for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
		{
			NewTags.Add(FName(*TagValue->AsString()));
		}

		bool bChanged = Actor->Tags.Num() != NewTags.Num();
		if (!bChanged)
		{
			for (int32 TagIndex = 0; TagIndex < NewTags.Num(); ++TagIndex)
			{
				if (Actor->Tags[TagIndex] != NewTags[TagIndex])
				{
					bChanged = true;
					break;
				}
			}
		}

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		SetChangedFlag(OutResult, bChanged);
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		Actor->Tags = NewTags;
		return true;
	}

	bool SetVisibility(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		bool bVisible = true;
		if (!Operation->TryGetBoolField(TEXT("visible"), bVisible))
		{
			OutError = TEXT("'visible' is required for set_visibility");
			return false;
		}

		const bool bCurrentlyVisible = !Actor->IsHiddenEd() && !Actor->IsTemporarilyHiddenInEditor();
		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetBoolField(TEXT("visible"), bVisible);
		SetChangedFlag(OutResult, bCurrentlyVisible != bVisible);
		if (bDryRun)
		{
			return true;
		}

		if (InOutWorld->WorldType != EWorldType::Editor)
		{
			OutWarnings.Add(MakeShareable(new FJsonValueString(
				FString::Printf(TEXT("set_visibility only controls editor visibility; '%s' was skipped because the resolved world is not an editor world."),
					*Actor->GetActorNameOrLabel()))));
			SetChangedFlag(OutResult, false);
			return true;
		}

		Actor->Modify();
		Actor->SetIsTemporarilyHiddenInEditor(!bVisible);
		return true;
	}

	bool SetMobility(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		FString MobilityString;
		if (!Operation->TryGetStringField(TEXT("mobility"), MobilityString))
		{
			OutError = TEXT("'mobility' is required for set_mobility");
			return false;
		}

		EComponentMobility::Type DesiredMobility = EComponentMobility::Static;
		if (!LevelActorToolUtils::TryParseMobility(MobilityString, DesiredMobility))
		{
			OutError = TEXT("'mobility' must be 'Static', 'Stationary', or 'Movable'");
			return false;
		}

		USceneComponent* RootSceneComponent = Actor->GetRootComponent();
		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetStringField(TEXT("mobility"), MobilityString);
		if (!RootSceneComponent)
		{
			OutWarnings.Add(MakeShareable(new FJsonValueString(
				FString::Printf(TEXT("Actor '%s' has no root scene component; set_mobility was skipped."), *Actor->GetActorNameOrLabel()))));
			SetChangedFlag(OutResult, false);
			return true;
		}

		SetChangedFlag(OutResult, RootSceneComponent->Mobility != DesiredMobility);
		if (bDryRun)
		{
			return true;
		}

		RootSceneComponent->Modify();
		RootSceneComponent->SetMobility(DesiredMobility);
		return true;
	}

	bool ReparentActor(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		return AttachLikeOperation(
			TEXT("parent_actor"),
			TEXT("parent_actor_handle"),
			InOutWorld,
			Operation,
			WorldType,
			Context,
			bDryRun,
			OutResult,
			OutError);
	}

	bool AddActorComponent(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		FString ComponentClassName;
		FString ComponentName;
		if (!Operation->TryGetStringField(TEXT("component_class"), ComponentClassName) ||
			!Operation->TryGetStringField(TEXT("component_name"), ComponentName))
		{
			OutError = TEXT("'component_class' and 'component_name' are required for add_component");
			return false;
		}

		UClass* ComponentClass = nullptr;
		if (!ResolveComponentClass(ComponentClassName, ComponentClass, OutError))
		{
			return false;
		}

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetStringField(TEXT("component_name"), ComponentName);
		SetChangedFlag(OutResult, true);
		if (bDryRun)
		{
			return true;
		}

		Actor->Modify();
		UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, ComponentClass, FName(*ComponentName), RF_Transactional);
		if (!NewComponent)
		{
			OutError = TEXT("Failed to create component");
			return false;
		}

		Actor->AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		return true;
	}

	bool RemoveActorComponent(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError)
	{
		AActor* Actor = nullptr;
		FString ErrorCode;
		TSharedPtr<FJsonObject> ErrorDetails;
		if (!ResolveTargetActor(Operation, WorldType, Context, InOutWorld, Actor, ErrorCode, OutError, ErrorDetails))
		{
			return false;
		}

		FString ComponentName;
		if (!Operation->TryGetStringField(TEXT("component_name"), ComponentName))
		{
			OutError = TEXT("'component_name' is required for remove_component");
			return false;
		}

		UActorComponent* Component = FMcpAssetModifier::FindComponentByName(Actor, ComponentName);
		if (!Component)
		{
			OutError = FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *Actor->GetActorNameOrLabel());
			return false;
		}

		OutResult->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		OutResult->SetStringField(TEXT("component_name"), ComponentName);
		SetChangedFlag(OutResult, true);
		if (bDryRun)
		{
			return true;
		}

		Component->Modify();
		Component->DestroyComponent();
		return true;
	}

	bool ExecuteOperation(
		UWorld*& InOutWorld,
		const TSharedPtr<FJsonObject>& Operation,
		const FString& WorldType,
		const FMcpToolContext& Context,
		bool bDryRun,
		TSharedPtr<FJsonObject>& OutResult,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		FString& OutError)
	{
		FString ActionName;
		if (!Operation->TryGetStringField(TEXT("action"), ActionName))
		{
			OutError = TEXT("Missing 'action' field");
			return false;
		}

		OutResult->SetStringField(TEXT("action"), ActionName);

		if (ActionName == TEXT("spawn_actor"))
		{
			return SpawnActor(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("delete_actor"))
		{
			return DeleteActor(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("duplicate_actor"))
		{
			return DuplicateActor(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("set_transform"))
		{
			return SetTransform(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("attach_actor"))
		{
			return AttachLikeOperation(TEXT("parent_actor"), TEXT("parent_actor_handle"), InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("detach_actor"))
		{
			return DetachActor(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("set_folder"))
		{
			return SetFolder(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("set_label"))
		{
			return SetLabel(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("add_component"))
		{
			return AddActorComponent(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("remove_component"))
		{
			return RemoveActorComponent(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("set_tags"))
		{
			return SetTags(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}
		if (ActionName == TEXT("set_visibility"))
		{
			return SetVisibility(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutWarnings, OutError);
		}
		if (ActionName == TEXT("set_mobility"))
		{
			return SetMobility(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutWarnings, OutError);
		}
		if (ActionName == TEXT("reparent"))
		{
			return ReparentActor(InOutWorld, Operation, WorldType, Context, bDryRun, OutResult, OutError);
		}

		OutError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		return false;
	}
}

FString UEditLevelBatchTool::GetToolDescription() const
{
	return TEXT("Transactional level batch editing for actors, folders, hierarchy, visibility, mobility, and spatial authoring. "
		"Accepts 'operations[]' as the public batch envelope and continues to accept legacy 'actions[]'.");
}

TMap<FString, FMcpSchemaProperty> UEditLevelBatchTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("Target world: 'editor', 'pie', or 'auto'"),
		{ TEXT("editor"), TEXT("pie"), TEXT("auto") }));

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
	PropertiesSchema->Description = TEXT("Property overrides");
	PropertiesSchema->RawSchema = PropertiesRawSchema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Level actor batch operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Level actor batch action"),
		{
			TEXT("spawn_actor"),
			TEXT("delete_actor"),
			TEXT("duplicate_actor"),
			TEXT("set_transform"),
			TEXT("attach_actor"),
			TEXT("detach_actor"),
			TEXT("set_folder"),
			TEXT("set_label"),
			TEXT("add_component"),
			TEXT("remove_component"),
			TEXT("set_tags"),
			TEXT("set_visibility"),
			TEXT("set_mobility"),
			TEXT("reparent")
		},
		true)));
	OperationSchema->Properties.Add(TEXT("actor_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor class path for spawn_actor"))));
	OperationSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor name or label"))));
	OperationSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle returned by query-level-summary or query-actor-selection"))));
	OperationSchema->Properties.Add(TEXT("actor_label"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor label for spawn_actor or set_label"))));
	OperationSchema->Properties.Add(TEXT("parent_actor"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Parent actor name for attach_actor or reparent"))));
	OperationSchema->Properties.Add(TEXT("parent_actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Parent actor handle for attach_actor or reparent"))));
	OperationSchema->Properties.Add(TEXT("transform"), TransformSchema);
	OperationSchema->Properties.Add(TEXT("folder_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("World Outliner folder path"))));
	OperationSchema->Properties.Add(TEXT("tags"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Actor tags"), TEXT("string"))));
	OperationSchema->Properties.Add(TEXT("properties"), PropertiesSchema);
	OperationSchema->Properties.Add(TEXT("select_after_spawn"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Select the actor after spawn in the editor world"))));
	OperationSchema->Properties.Add(TEXT("new_actor_label"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New label for duplicate_actor"))));
	OperationSchema->Properties.Add(TEXT("offset"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Duplicate offset [X,Y,Z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("socket_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional attachment socket name"))));
	OperationSchema->Properties.Add(TEXT("keep_world_transform"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Keep world transform when attaching, detaching, or reparenting (default true)"))));
	OperationSchema->Properties.Add(TEXT("component_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component class path for add_component"))));
	OperationSchema->Properties.Add(TEXT("component_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component name for add_component or remove_component"))));
	OperationSchema->Properties.Add(TEXT("visible"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Visible flag for set_visibility"))));
	OperationSchema->Properties.Add(TEXT("mobility"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Mobility for set_mobility"),
		{ TEXT("Static"), TEXT("Stationary"), TEXT("Movable") })));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Primary batch envelope for level operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	FMcpSchemaProperty ActionsSchema = OperationsSchema;
	ActionsSchema.Description = TEXT("Legacy compatibility alias for operations");
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after edits. Ignored for PIE worlds.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transaction label")));

	return Schema;
}

TArray<FString> UEditLevelBatchTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UEditLevelBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Level Batch"));

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	if (!EditLevelBatchToolPrivate::TryGetOperationArray(Arguments, OperationsArray, ErrorCode, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
	}

	UWorld* ActiveWorld = nullptr;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < OperationsArray->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonValue>& OperationValue = (*OperationsArray)[OperationIndex];
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!OperationValue.IsValid() || !OperationValue->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Operation at index %d is not a valid object"), OperationIndex));
			}
			continue;
		}

		TSharedPtr<FJsonObject> OperationResult = MakeShareable(new FJsonObject);
		OperationResult->SetNumberField(TEXT("index"), OperationIndex);

		FString OperationError;
		const bool bSuccess = EditLevelBatchToolPrivate::ExecuteOperation(
			ActiveWorld,
			*OperationObject,
			WorldType,
			Context,
			bDryRun,
			OperationResult,
			WarningsArray,
			OperationError);

		OperationResult->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			OperationResult->SetStringField(TEXT("error"), OperationError);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
				PartialResult->SetArrayField(TEXT("results"), ResultsArray);
				PartialResult->SetArrayField(TEXT("warnings"), WarningsArray);
				PartialResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
				PartialResult->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
				PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), OperationError, nullptr, PartialResult);
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
	}

	if (!LevelActorToolUtils::SaveWorldIfNeeded(ActiveWorld, bSave, WarningsArray, ModifiedAssetsArray, ErrorCode, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
