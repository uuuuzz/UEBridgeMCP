// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/GameplayAdvancedTools.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

namespace
{
	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() < 3)
		{
			return false;
		}

		OutVector.X = static_cast<float>((*Values)[0]->AsNumber());
		OutVector.Y = static_cast<float>((*Values)[1]->AsNumber());
		OutVector.Z = static_cast<float>((*Values)[2]->AsNumber());
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> VectorToJsonArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	TArray<TSharedPtr<FJsonValue>> SerializePathPoints(const TArray<FVector>& Points)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FVector& Point : Points)
		{
			Result.Add(MakeShareable(new FJsonValueArray(VectorToJsonArray(Point))));
		}
		return Result;
	}

	bool ApplyPropertyMap(UObject* Target, const TSharedPtr<FJsonObject>& PropertiesObject, bool bApply, FString& OutError)
	{
		if (!Target || !PropertiesObject.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
		{
			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString PropertyError;
			if (!FMcpAssetModifier::FindPropertyByPath(Target, Pair.Key, Property, Container, PropertyError))
			{
				OutError = FString::Printf(TEXT("Property '%s' not found: %s"), *Pair.Key, *PropertyError);
				return false;
			}
			if (bApply && !FMcpAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, PropertyError))
			{
				OutError = FString::Printf(TEXT("Failed to set property '%s': %s"), *Pair.Key, *PropertyError);
				return false;
			}
		}
		return true;
	}

	UActorComponent* FindBlueprintComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName)
	{
		if (!Blueprint || ComponentName.IsEmpty() || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}

		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}

			UActorComponent* ComponentTemplate = nullptr;
			if (BPGC)
			{
				ComponentTemplate = Node->GetActualComponentTemplate(BPGC);
			}
			else
			{
				ComponentTemplate = Node->ComponentTemplate.Get();
			}
			if (ComponentTemplate && (Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) || ComponentTemplate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
			{
				return ComponentTemplate;
			}
		}
		return nullptr;
	}

	void SetComponentReplicatesForTemplate(UActorComponent* Component, bool bReplicates)
	{
		if (!Component)
		{
			return;
		}

		if (FBoolProperty* ReplicatesProperty = FindFProperty<FBoolProperty>(UActorComponent::StaticClass(), TEXT("bReplicates")))
		{
			ReplicatesProperty->SetPropertyValue_InContainer(Component, bReplicates);
			return;
		}

		Component->SetIsReplicated(bReplicates);
	}

	TSharedPtr<FJsonObject> SerializeComponentReplication(UActorComponent* Component)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Component)
		{
			return Object;
		}
		Object->SetStringField(TEXT("component_name"), Component->GetName());
		Object->SetStringField(TEXT("class"), Component->GetClass()->GetPathName());
		Object->SetBoolField(TEXT("replicates"), Component->GetIsReplicated());
		Object->SetBoolField(TEXT("name_stable_for_networking"), Component->IsNameStableForNetworking());
		Object->SetBoolField(TEXT("supported_for_networking"), Component->IsSupportedForNetworking());
		return Object;
	}

	UClass* BlackboardKeyClassFromName(const FString& TypeName)
	{
		const FString Lower = TypeName.ToLower();
		if (Lower == TEXT("bool") || Lower == TEXT("boolean")) return UBlackboardKeyType_Bool::StaticClass();
		if (Lower == TEXT("int") || Lower == TEXT("integer")) return UBlackboardKeyType_Int::StaticClass();
		if (Lower == TEXT("float") || Lower == TEXT("number")) return UBlackboardKeyType_Float::StaticClass();
		if (Lower == TEXT("name")) return UBlackboardKeyType_Name::StaticClass();
		if (Lower == TEXT("string")) return UBlackboardKeyType_String::StaticClass();
		if (Lower == TEXT("vector")) return UBlackboardKeyType_Vector::StaticClass();
		if (Lower == TEXT("rotator")) return UBlackboardKeyType_Rotator::StaticClass();
		if (Lower == TEXT("object")) return UBlackboardKeyType_Object::StaticClass();
		if (Lower == TEXT("class")) return UBlackboardKeyType_Class::StaticClass();
		if (Lower == TEXT("enum")) return UBlackboardKeyType_Enum::StaticClass();
		if (TypeName.StartsWith(TEXT("/Script/")) || TypeName.Contains(TEXT(".")))
		{
			return LoadClass<UBlackboardKeyType>(nullptr, *TypeName);
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> SerializeBlackboardKey(const FBlackboardEntry& Key, int32 Index)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("name"), Key.EntryName.ToString());
		Object->SetBoolField(TEXT("instance_synced"), Key.bInstanceSynced != 0);
		Object->SetStringField(TEXT("key_type"), Key.KeyType ? Key.KeyType->GetClass()->GetPathName() : TEXT(""));
#if WITH_EDITORONLY_DATA
		Object->SetStringField(TEXT("description"), Key.EntryDescription);
		Object->SetStringField(TEXT("category"), Key.EntryCategory.ToString());
#endif
		if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Key.KeyType))
		{
			Object->SetStringField(TEXT("base_class"), ObjectKey->BaseClass ? ObjectKey->BaseClass->GetPathName() : TEXT(""));
		}
		if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Key.KeyType))
		{
			Object->SetStringField(TEXT("base_class"), ClassKey->BaseClass ? ClassKey->BaseClass->GetPathName() : TEXT(""));
		}
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SerializeBlackboardKeys(const UBlackboardData* Blackboard)
	{
		TArray<TSharedPtr<FJsonValue>> KeysArray;
		if (!Blackboard)
		{
			return KeysArray;
		}

		const TArray<FBlackboardEntry>& Keys = Blackboard->GetKeys();
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			KeysArray.Add(MakeShareable(new FJsonValueObject(SerializeBlackboardKey(Keys[Index], Index))));
		}
		return KeysArray;
	}

	TSharedPtr<FJsonObject> SerializeAIAsset(UObject* Asset, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("asset_type"), Asset ? Asset->GetClass()->GetPathName() : TEXT(""));

		if (const UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Asset))
		{
			Object->SetStringField(TEXT("blackboard_asset_path"), BehaviorTree->GetBlackboardAsset() ? BehaviorTree->GetBlackboardAsset()->GetPathName() : TEXT(""));
			Object->SetStringField(TEXT("root_node_class"), BehaviorTree->RootNode ? BehaviorTree->RootNode->GetClass()->GetPathName() : TEXT(""));
			Object->SetNumberField(TEXT("root_decorator_count"), BehaviorTree->RootDecorators.Num());
#if WITH_EDITORONLY_DATA
			Object->SetNumberField(TEXT("editor_graph_node_count"), BehaviorTree->BTGraph ? BehaviorTree->BTGraph->Nodes.Num() : 0);
#endif
		}
		else if (const UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
		{
			Object->SetStringField(TEXT("parent_blackboard"), Blackboard->Parent ? Blackboard->Parent->GetPathName() : TEXT(""));
			Object->SetNumberField(TEXT("key_count"), Blackboard->GetKeys().Num());
			Object->SetArrayField(TEXT("keys"), SerializeBlackboardKeys(Blackboard));
			Object->SetBoolField(TEXT("valid"), Blackboard->IsValid());
		}
		else if (Asset)
		{
			Object->SetBoolField(TEXT("recognized_ai_asset"), Asset->GetClass()->GetPathName().Contains(TEXT("EnvQuery")));
		}

		return Object;
	}
}

FString UQueryNavigationPathTool::GetToolDescription() const
{
	return TEXT("Find a synchronous navigation path between two world locations and return path points, length, cost, and partial state.");
}

TMap<FString, FMcpSchemaProperty> UQueryNavigationPathTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("start"), FMcpSchemaProperty::MakeArray(TEXT("Start location [x,y,z]"), TEXT("number")));
	Schema.Add(TEXT("end"), FMcpSchemaProperty::MakeArray(TEXT("End location [x,y,z]"), TEXT("number")));
	Schema.Add(TEXT("pathfinding_context_actor"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor name used as pathfinding context")));
	return Schema;
}

FMcpToolResult UQueryNavigationPathTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	FVector Start;
	FVector End;
	if (!ReadVector(Arguments, TEXT("start"), Start) || !ReadVector(Arguments, TEXT("end"), End))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'start' and 'end' arrays are required"));
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	AActor* PathfindingContext = nullptr;
	const FString ContextActorName = GetStringArgOrDefault(Arguments, TEXT("pathfinding_context_actor"));
	if (!ContextActorName.IsEmpty())
	{
		PathfindingContext = FMcpAssetModifier::FindActorByName(World, ContextActorName);
		if (!PathfindingContext)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), FString::Printf(TEXT("Pathfinding context actor not found: %s"), *ContextActorName));
		}
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, End, PathfindingContext);
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), Path && Path->IsValid());
	Response->SetStringField(TEXT("world_name"), World->GetName());
	Response->SetArrayField(TEXT("start"), VectorToJsonArray(Start));
	Response->SetArrayField(TEXT("end"), VectorToJsonArray(End));
	Response->SetBoolField(TEXT("valid"), Path && Path->IsValid());
	Response->SetBoolField(TEXT("partial"), Path ? Path->IsPartial() : false);
	Response->SetNumberField(TEXT("path_length"), Path ? Path->GetPathLength() : 0.0);
	Response->SetNumberField(TEXT("path_cost"), Path ? Path->GetPathCost() : 0.0);
	Response->SetNumberField(TEXT("point_count"), Path ? Path->PathPoints.Num() : 0);
	Response->SetArrayField(TEXT("points"), Path ? SerializePathPoints(Path->PathPoints) : TArray<TSharedPtr<FJsonValue>>());
	return FMcpToolResult::StructuredJson(Response, !(Path && Path->IsValid()));
}

FString UEditNavigationBuildTool::GetToolDescription() const
{
	return TEXT("Trigger or cancel navigation building for the target world.");
}

TMap<FString, FMcpSchemaProperty> UEditNavigationBuildTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("action"), FMcpSchemaProperty::MakeEnum(TEXT("Navigation build action"), { TEXT("build"), TEXT("cancel") }));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without invoking the navigation system")));
	return Schema;
}

FMcpToolResult UEditNavigationBuildTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString Action = GetStringArgOrDefault(Arguments, TEXT("action"), TEXT("build")).ToLower();
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SUBSYSTEM_UNAVAILABLE"), TEXT("NavigationSystemV1 is not available for the target world"));
	}

	if (!bDryRun)
	{
		if (Action == TEXT("build"))
		{
			NavigationSystem->Build();
		}
		else if (Action == TEXT("cancel"))
		{
			NavigationSystem->CancelBuild();
		}
		else
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'action' must be 'build' or 'cancel'"));
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("world_name"), World->GetName());
	Response->SetStringField(TEXT("action"), Action);
	Response->SetBoolField(TEXT("can_rebuild_dirty_navigation"), NavigationSystem->CanRebuildDirtyNavigation());
	Response->SetBoolField(TEXT("build_in_progress"), NavigationSystem->IsNavigationBuildInProgress());
	Response->SetNumberField(TEXT("remaining_build_tasks"), NavigationSystem->GetNumRemainingBuildTasks());
	Response->SetNumberField(TEXT("running_build_tasks"), NavigationSystem->GetNumRunningBuildTasks());
	return FMcpToolResult::StructuredJson(Response);
}

FString UQueryNetworkComponentSettingsTool::GetToolDescription() const
{
	return TEXT("Query replication settings on an actor component template in a Blueprint or on a placed actor component.");
}

TMap<FString, FMcpSchemaProperty> UQueryNetworkComponentSettingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("target_type"), FMcpSchemaProperty::MakeEnum(TEXT("Target type"), { TEXT("asset"), TEXT("actor") }));
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path when target_type is asset")));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world for actor queries"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label when target_type is actor")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component template or instance name"), true));
	return Schema;
}

FMcpToolResult UQueryNetworkComponentSettingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString TargetType = GetStringArgOrDefault(Arguments, TEXT("target_type"), TEXT("asset")).ToLower();
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	if (ComponentName.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'component_name' is required"));
	}

	UActorComponent* Component = nullptr;
	FString TargetName;
	if (TargetType == TEXT("asset"))
	{
		const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
		FString LoadError;
		UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
		if (!Blueprint)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}

		Component = FindBlueprintComponentTemplate(Blueprint, ComponentName);
		TargetName = AssetPath;
	}
	else if (TargetType == TEXT("actor"))
	{
		UWorld* World = FMcpAssetModifier::ResolveWorld(GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto")));
		if (!World)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
		}

		AActor* Actor = FMcpAssetModifier::FindActorByName(World, GetStringArgOrDefault(Arguments, TEXT("actor_name")));
		if (!Actor)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), TEXT("Actor not found"));
		}

		Component = FMcpAssetModifier::FindComponentByName(Actor, ComponentName);
		TargetName = Actor->GetActorNameOrLabel();
	}
	else
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'target_type' must be 'asset' or 'actor'"));
	}

	if (!Component)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), FString::Printf(TEXT("Component not found: %s"), *ComponentName));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("target_type"), TargetType);
	Response->SetStringField(TEXT("target"), TargetName);
	Response->SetObjectField(TEXT("component"), SerializeComponentReplication(Component));
	return FMcpToolResult::StructuredJson(Response);
}

FString UEditNetworkComponentSettingsTool::GetToolDescription() const
{
	return TEXT("Edit replication settings on an actor component template in a Blueprint or on a placed actor component.");
}

TMap<FString, FMcpSchemaProperty> UEditNetworkComponentSettingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("target_type"), FMcpSchemaProperty::MakeEnum(TEXT("Target type"), { TEXT("asset"), TEXT("actor") }));
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path when target_type is asset")));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world for actor edits"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label when target_type is actor")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component template or instance name"), true));
	Schema.Add(TEXT("replicates"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Set component replication")));
	Schema.Add(TEXT("net_addressable"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Mark component net-addressable")));
	Schema.Add(TEXT("properties"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Additional reflected property map")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile Blueprint after asset edits. Default: true.")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save Blueprint after asset edits. Default: true.")));
	return Schema;
}

FMcpToolResult UEditNetworkComponentSettingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString TargetType = GetStringArgOrDefault(Arguments, TEXT("target_type"), TEXT("asset")).ToLower();
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	if (ComponentName.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'component_name' is required"));
	}

	UObject* OwningAsset = nullptr;
	UBlueprint* OwningBlueprint = nullptr;
	UActorComponent* Component = nullptr;
	FString TargetName;

	if (TargetType == TEXT("asset"))
	{
		const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
		FString LoadError;
		OwningBlueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
		if (!OwningBlueprint)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}
		Component = FindBlueprintComponentTemplate(OwningBlueprint, ComponentName);
		OwningAsset = OwningBlueprint;
		TargetName = AssetPath;
	}
	else if (TargetType == TEXT("actor"))
	{
		UWorld* World = FMcpAssetModifier::ResolveWorld(GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto")));
		if (!World)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
		}
		AActor* Actor = FMcpAssetModifier::FindActorByName(World, GetStringArgOrDefault(Arguments, TEXT("actor_name")));
		if (!Actor)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), TEXT("Actor not found"));
		}
		Component = FMcpAssetModifier::FindComponentByName(Actor, ComponentName);
		OwningAsset = Actor;
		TargetName = Actor->GetActorNameOrLabel();
	}
	else
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'target_type' must be 'asset' or 'actor'"));
	}

	if (!Component)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), FString::Printf(TEXT("Component not found: %s"), *ComponentName));
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	Arguments->TryGetObjectField(TEXT("properties"), PropertiesObject);

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Network Component Settings")));
		Component->Modify();
		if (OwningAsset)
		{
			OwningAsset->Modify();
		}
	}

	bool bChanged = false;
	bool bReplicates = false;
	if (GetBoolArg(Arguments, TEXT("replicates"), bReplicates))
	{
		if (!bDryRun)
		{
			if (TargetType == TEXT("asset"))
			{
				SetComponentReplicatesForTemplate(Component, bReplicates);
			}
			else
			{
				Component->SetIsReplicated(bReplicates);
			}
		}
		bChanged = true;
	}

	bool bNetAddressable = false;
	if (GetBoolArg(Arguments, TEXT("net_addressable"), bNetAddressable) && bNetAddressable)
	{
		if (!bDryRun)
		{
			Component->SetNetAddressable();
		}
		bChanged = true;
	}

	FString PropertyError;
	if (PropertiesObject && PropertiesObject->IsValid() && !ApplyPropertyMap(Component, *PropertiesObject, !bDryRun, PropertyError))
	{
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), PropertyError);
	}
	if (PropertiesObject && PropertiesObject->IsValid())
	{
		bChanged = true;
	}

	if (!bDryRun && bChanged)
	{
		if (OwningBlueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(OwningBlueprint);
			if (bCompile)
			{
				FString CompileError;
				if (!FMcpAssetModifier::CompileBlueprint(OwningBlueprint, CompileError))
				{
					if (Transaction.IsValid())
					{
						Transaction->Cancel();
					}
					return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
				}
			}
			FMcpAssetModifier::MarkPackageDirty(OwningBlueprint);
			if (bSave)
			{
				FString SaveError;
				if (!FMcpAssetModifier::SaveAsset(OwningBlueprint, false, SaveError))
				{
					if (Transaction.IsValid())
					{
						Transaction->Cancel();
					}
					return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
				}
			}
		}
		else if (OwningAsset)
		{
			FMcpAssetModifier::MarkPackageDirty(OwningAsset);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("target_type"), TargetType);
	Response->SetStringField(TEXT("target"), TargetName);
	Response->SetObjectField(TEXT("component"), SerializeComponentReplication(Component));
	return FMcpToolResult::StructuredJson(Response);
}

FString UQueryAIBehaviorAssetsTool::GetToolDescription() const
{
	return TEXT("Query BehaviorTree, Blackboard, and EnvQuery-like AI behavior assets by asset path or content root.");
}

TMap<FString, FMcpSchemaProperty> UQueryAIBehaviorAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional AI asset path to inspect")));
	Schema.Add(TEXT("root_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Content root to scan when asset_path is omitted. Default: /Game")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum assets to return when scanning")));
	return Schema;
}

FMcpToolResult UQueryAIBehaviorAssetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (!AssetPath.IsEmpty())
	{
		FString LoadError;
		UObject* Asset = FMcpAssetModifier::LoadAssetByPath<UObject>(AssetPath, LoadError);
		if (!Asset)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}
		return FMcpToolResult::StructuredSuccess(SerializeAIAsset(Asset, AssetPath), TEXT("AI asset summary ready"));
	}

	const FString RootPath = GetStringArgOrDefault(Arguments, TEXT("root_path"), TEXT("/Game"));
	const int32 Limit = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("limit"), 100));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*RootPath), AssetDataList, true);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (AssetsArray.Num() >= Limit)
		{
			break;
		}

		const FString ClassPath = AssetData.AssetClassPath.ToString();
		if (!ClassPath.Contains(TEXT("BehaviorTree")) && !ClassPath.Contains(TEXT("BlackboardData")) && !ClassPath.Contains(TEXT("EnvQuery")))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		Object->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		Object->SetStringField(TEXT("asset_class"), ClassPath);
		AssetsArray.Add(MakeShareable(new FJsonValueObject(Object)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("root_path"), RootPath);
	Response->SetArrayField(TEXT("assets"), AssetsArray);
	Response->SetNumberField(TEXT("asset_count"), AssetsArray.Num());
	return FMcpToolResult::StructuredJson(Response);
}

FString UEditBlackboardKeysTool::GetToolDescription() const
{
	return TEXT("Batch add, remove, rename, or update Blackboard keys.");
}

TMap<FString, FMcpSchemaProperty> UEditBlackboardKeysTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("BlackboardData asset path"), true));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("Blackboard key operations"), TEXT("object")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the asset after edits. Default: true.")));
	return Schema;
}

FMcpToolResult UEditBlackboardKeysTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UBlackboardData* Blackboard = FMcpAssetModifier::LoadAssetByPath<UBlackboardData>(AssetPath, LoadError);
	if (!Blackboard)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Blackboard Keys")));
		Blackboard->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bChanged = false;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		Action = Action.ToLower();

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bOperationSuccess = false;
		FString OperationError;

		if (Action == TEXT("add_key"))
		{
			const FString KeyName = GetStringArgOrDefault(*OperationObject, TEXT("key_name"));
			const FString KeyType = GetStringArgOrDefault(*OperationObject, TEXT("key_type"));
			UClass* KeyClass = BlackboardKeyClassFromName(KeyType);
			if (KeyName.IsEmpty() || !KeyClass)
			{
				OperationError = TEXT("'key_name' and valid 'key_type' are required");
			}
			else if (Blackboard->Keys.ContainsByPredicate([&](const FBlackboardEntry& Entry) { return Entry.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase); }))
			{
				OperationError = TEXT("Blackboard key already exists");
			}
			else
			{
				if (!bDryRun)
				{
					FBlackboardEntry Entry;
					Entry.EntryName = FName(*KeyName);
					Entry.bInstanceSynced = GetBoolArgOrDefault(*OperationObject, TEXT("instance_synced"), false);
#if WITH_EDITORONLY_DATA
					Entry.EntryDescription = GetStringArgOrDefault(*OperationObject, TEXT("description"));
					Entry.EntryCategory = FName(*GetStringArgOrDefault(*OperationObject, TEXT("category")));
#endif
					Entry.KeyType = NewObject<UBlackboardKeyType>(Blackboard, KeyClass, NAME_None, RF_Transactional);

					const FString BaseClassPath = GetStringArgOrDefault(*OperationObject, TEXT("base_class"));
					if (!BaseClassPath.IsEmpty())
					{
						UClass* BaseClass = LoadClass<UObject>(nullptr, *BaseClassPath);
						if (UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
						{
							ObjectKey->BaseClass = BaseClass;
						}
						if (UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
						{
							ClassKey->BaseClass = BaseClass;
						}
					}

					Blackboard->Keys.Add(Entry);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("remove_key"))
		{
			const FString KeyName = GetStringArgOrDefault(*OperationObject, TEXT("key_name"));
			const int32 KeyIndex = Blackboard->Keys.IndexOfByPredicate([&](const FBlackboardEntry& Entry) { return Entry.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase); });
			if (KeyIndex == INDEX_NONE)
			{
				OperationError = TEXT("Blackboard key not found");
			}
			else
			{
				if (!bDryRun)
				{
					Blackboard->Keys.RemoveAt(KeyIndex);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("rename_key"))
		{
			const FString OldName = GetStringArgOrDefault(*OperationObject, TEXT("old_name"));
			const FString NewName = GetStringArgOrDefault(*OperationObject, TEXT("new_name"));
			const int32 KeyIndex = Blackboard->Keys.IndexOfByPredicate([&](const FBlackboardEntry& Entry) { return Entry.EntryName.ToString().Equals(OldName, ESearchCase::IgnoreCase); });
			if (KeyIndex == INDEX_NONE || NewName.IsEmpty())
			{
				OperationError = TEXT("Existing old_name and non-empty new_name are required");
			}
			else
			{
				if (!bDryRun)
				{
					Blackboard->Keys[KeyIndex].EntryName = FName(*NewName);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("set_instance_synced"))
		{
			const FString KeyName = GetStringArgOrDefault(*OperationObject, TEXT("key_name"));
			const int32 KeyIndex = Blackboard->Keys.IndexOfByPredicate([&](const FBlackboardEntry& Entry) { return Entry.EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase); });
			if (KeyIndex == INDEX_NONE)
			{
				OperationError = TEXT("Blackboard key not found");
			}
			else
			{
				if (!bDryRun)
				{
					Blackboard->Keys[KeyIndex].bInstanceSynced = GetBoolArgOrDefault(*OperationObject, TEXT("instance_synced"), false);
				}
				bOperationSuccess = true;
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		if (bOperationSuccess)
		{
			bChanged = true;
		}
		else
		{
			bAnyFailed = true;
			ResultObject->SetStringField(TEXT("error"), OperationError);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		Blackboard->UpdateParentKeys();
		Blackboard->UpdateKeyIDs();
		Blackboard->UpdateIfHasSynchronizedKeys();
		Blackboard->PropagateKeyChangesToDerivedBlackboardAssets();
		FMcpAssetModifier::MarkPackageDirty(Blackboard);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blackboard, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("keys"), SerializeBlackboardKeys(Blackboard));
	Response->SetNumberField(TEXT("key_count"), Blackboard->GetKeys().Num());
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
