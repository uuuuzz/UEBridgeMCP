// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PCG/PCGGraphTools.h"

#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "PCGCommon.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "ScopedTransaction.h"

namespace
{
	UClass* ResolvePCGSettingsClass(const FString& ClassNameOrPath)
	{
		if (ClassNameOrPath.IsEmpty())
		{
			return nullptr;
		}

		if (ClassNameOrPath.StartsWith(TEXT("/Script/")) || ClassNameOrPath.Contains(TEXT(".")))
		{
			return LoadClass<UPCGSettings>(nullptr, *ClassNameOrPath);
		}

		const FString Lower = ClassNameOrPath.ToLower();
		TMap<FString, FString> Aliases;
		Aliases.Add(TEXT("transform_points"), TEXT("/Script/PCG.PCGTransformPointsSettings"));
		Aliases.Add(TEXT("surface_sampler"), TEXT("/Script/PCG.PCGSurfaceSamplerSettings"));
		Aliases.Add(TEXT("volume_sampler"), TEXT("/Script/PCG.PCGVolumeSamplerSettings"));
		Aliases.Add(TEXT("static_mesh_spawner"), TEXT("/Script/PCG.PCGStaticMeshSpawnerSettings"));
		Aliases.Add(TEXT("point_from_mesh"), TEXT("/Script/PCG.PCGPointFromMeshSettings"));

		if (const FString* Path = Aliases.Find(Lower))
		{
			return LoadClass<UPCGSettings>(nullptr, **Path);
		}

		return LoadClass<UPCGSettings>(nullptr, *FString::Printf(TEXT("/Script/PCG.%s"), *ClassNameOrPath));
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

	TSharedPtr<FJsonObject> SerializePCGNode(const UPCGNode* Node, int32 Index)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Node)
		{
			return Object;
		}

		const UPCGSettings* Settings = Node->GetSettings();
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("object_name"), Node->GetName());
		Object->SetStringField(TEXT("authored_title"), Node->GetAuthoredTitleName().ToString());
		Object->SetStringField(TEXT("settings_class"), Settings ? Settings->GetClass()->GetPathName() : TEXT(""));

		int32 PositionX = 0;
		int32 PositionY = 0;
		Node->GetNodePosition(PositionX, PositionY);
		Object->SetNumberField(TEXT("position_x"), PositionX);
		Object->SetNumberField(TEXT("position_y"), PositionY);

		TArray<TSharedPtr<FJsonValue>> InputPinsArray;
		for (const UPCGPin* Pin : Node->GetInputPins())
		{
			TSharedPtr<FJsonObject> PinObject = MakeShareable(new FJsonObject);
			PinObject->SetStringField(TEXT("label"), Pin ? Pin->Properties.Label.ToString() : TEXT(""));
			PinObject->SetNumberField(TEXT("edge_count"), Pin ? Pin->EdgeCount() : 0);
			InputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObject)));
		}
		Object->SetArrayField(TEXT("input_pins"), InputPinsArray);

		TArray<TSharedPtr<FJsonValue>> OutputPinsArray;
		for (const UPCGPin* Pin : Node->GetOutputPins())
		{
			TSharedPtr<FJsonObject> PinObject = MakeShareable(new FJsonObject);
			PinObject->SetStringField(TEXT("label"), Pin ? Pin->Properties.Label.ToString() : TEXT(""));
			PinObject->SetNumberField(TEXT("edge_count"), Pin ? Pin->EdgeCount() : 0);
			OutputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObject)));
		}
		Object->SetArrayField(TEXT("output_pins"), OutputPinsArray);

		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SerializePCGNodes(const UPCGGraph* Graph)
	{
		TArray<TSharedPtr<FJsonValue>> NodesArray;
		if (!Graph)
		{
			return NodesArray;
		}

		const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			NodesArray.Add(MakeShareable(new FJsonValueObject(SerializePCGNode(Nodes[Index], Index))));
		}
		return NodesArray;
	}

	UPCGNode* ResolveNode(UPCGGraph* Graph, const TSharedPtr<FJsonObject>& Object, const FString& Prefix = TEXT("node"))
	{
		if (!Graph || !Object.IsValid())
		{
			return nullptr;
		}

		FString Ref;
		Object->TryGetStringField(Prefix, Ref);
		if (Ref.Equals(TEXT("input"), ESearchCase::IgnoreCase))
		{
			return Graph->GetInputNode();
		}
		if (Ref.Equals(TEXT("output"), ESearchCase::IgnoreCase))
		{
			return Graph->GetOutputNode();
		}

		int32 NodeIndex = INDEX_NONE;
		if (Object->TryGetNumberField(FString::Printf(TEXT("%s_index"), *Prefix), NodeIndex))
		{
			const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
			return Nodes.IsValidIndex(NodeIndex) ? Nodes[NodeIndex] : nullptr;
		}

		const FString TitleField = FString::Printf(TEXT("%s_title"), *Prefix);
		FString NodeTitle;
		Object->TryGetStringField(TitleField, NodeTitle);
		if (NodeTitle.IsEmpty())
		{
			NodeTitle = Ref;
		}

		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && (Node->GetName().Equals(NodeTitle, ESearchCase::IgnoreCase) || Node->GetAuthoredTitleName().ToString().Equals(NodeTitle, ESearchCase::IgnoreCase)))
			{
				return Node;
			}
		}
		return nullptr;
	}

	bool HasPinWithLabel(const UPCGNode* Node, const FName& PinLabel, bool bOutputPin)
	{
		if (!Node)
		{
			return false;
		}

		const TArray<UPCGPin*>& Pins = bOutputPin ? Node->GetOutputPins() : Node->GetInputPins();
		for (const UPCGPin* Pin : Pins)
		{
			if (Pin && Pin->Properties.Label == PinLabel)
			{
				return true;
			}
		}
		return false;
	}

	UPCGGraphInterface* LoadOrCreateGraph(const FString& AssetPath, bool bCreateIfMissing, bool bDryRun, bool& bOutCreated, FString& OutError)
	{
		bOutCreated = false;

		if (FMcpAssetModifier::AssetExists(AssetPath))
		{
			return FMcpAssetModifier::LoadAssetByPath<UPCGGraphInterface>(AssetPath, OutError);
		}

		if (!bCreateIfMissing)
		{
			OutError = FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath);
			return nullptr;
		}

		if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, OutError))
		{
			return nullptr;
		}

		bOutCreated = true;
		if (bDryRun)
		{
			return nullptr;
		}

		const FString AssetName = FPackageName::GetShortName(AssetPath);
		UPackage* Package = CreatePackage(*AssetPath);
		UPCGGraph* Graph = Package ? NewObject<UPCGGraph>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional) : nullptr;
		if (!Graph)
		{
			OutError = TEXT("Failed to create PCG graph asset");
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(Graph);
		FMcpAssetModifier::MarkPackageDirty(Graph);
		return Graph;
	}

	TSharedPtr<FJsonObject> BuildGraphSummary(const FString& AssetPath, UPCGGraphInterface* GraphInterface)
	{
		UPCGGraph* Graph = GraphInterface ? GraphInterface->GetMutablePCGGraph() : nullptr;

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("asset_path"), AssetPath);
		Response->SetStringField(TEXT("class"), GraphInterface ? GraphInterface->GetClass()->GetPathName() : TEXT(""));
		Response->SetBoolField(TEXT("is_instance"), GraphInterface ? GraphInterface->IsInstance() : false);
		Response->SetBoolField(TEXT("has_graph"), Graph != nullptr);
		Response->SetNumberField(TEXT("node_count"), Graph ? Graph->GetNodes().Num() : 0);
		Response->SetArrayField(TEXT("nodes"), SerializePCGNodes(Graph));
		return Response;
	}
}

FString UQueryPCGGraphSummaryTool::GetToolDescription() const
{
	return TEXT("Return PCG graph nodes, pins, settings classes, and basic graph metadata.");
}

TMap<FString, FMcpSchemaProperty> UQueryPCGGraphSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PCG graph asset path"), true));
	return Schema;
}

FMcpToolResult UQueryPCGGraphSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString LoadError;
	UPCGGraphInterface* GraphInterface = FMcpAssetModifier::LoadAssetByPath<UPCGGraphInterface>(AssetPath, LoadError);
	if (!GraphInterface)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FJsonObject> Response = BuildGraphSummary(AssetPath, GraphInterface);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	return FMcpToolResult::StructuredJson(Response);
}

FString UEditPCGGraphTool::GetToolDescription() const
{
	return TEXT("Create or batch-edit a PCG graph by adding/removing nodes, connecting pins, and setting reflected node settings.");
}

TMap<FString, FMcpSchemaProperty> UEditPCGGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PCG graph asset path"), true));
	Schema.Add(TEXT("create_if_missing"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create a standalone PCG graph if asset_path does not exist")));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("PCG graph edit operations"), TEXT("object")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the graph after edits. Default: true.")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction and skip saving when any operation fails. Default: true.")));
	return Schema;
}

FMcpToolResult UEditPCGGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bCreateIfMissing = GetBoolArgOrDefault(Arguments, TEXT("create_if_missing"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	bool bGraphCreated = false;
	FString GraphError;
	UPCGGraphInterface* GraphInterface = LoadOrCreateGraph(AssetPath, bCreateIfMissing, bDryRun, bGraphCreated, GraphError);
	if (!GraphInterface && !(bDryRun && bGraphCreated))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), GraphError);
	}

	UPCGGraph* Graph = GraphInterface ? GraphInterface->GetMutablePCGGraph() : nullptr;
	if (!Graph && !bDryRun)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_TYPE"), TEXT("Asset does not expose a mutable PCG graph"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun && GraphInterface)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit PCG Graph")));
		GraphInterface->Modify();
		if (Graph)
		{
			Graph->Modify();
		}
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bChanged = bGraphCreated;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		const FString Action = GetStringArgOrDefault(*OperationObject, TEXT("action")).ToLower();
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bOperationSuccess = false;
		FString OperationError;

		if (Action == TEXT("add_node"))
		{
			UClass* SettingsClass = ResolvePCGSettingsClass(GetStringArgOrDefault(*OperationObject, TEXT("settings_class")));
			if (!SettingsClass)
			{
				OperationError = TEXT("Valid settings_class is required");
			}
			else if (bDryRun)
			{
				const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
				if ((*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && PropertiesObject->IsValid())
				{
					UPCGSettings* DefaultSettings = Cast<UPCGSettings>(SettingsClass->GetDefaultObject());
					if (!DefaultSettings || !ApplyPropertyMap(DefaultSettings, *PropertiesObject, false, OperationError))
					{
						// OperationError is populated.
					}
					else
					{
						bOperationSuccess = true;
					}
				}
				else
				{
					bOperationSuccess = true;
				}
			}
			else if (!bDryRun)
			{
				UPCGSettings* Settings = nullptr;
				UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, Settings);
				if (!Node)
				{
					OperationError = TEXT("PCG graph rejected the new node");
				}
				else
				{
					const FString NodeTitle = GetStringArgOrDefault(*OperationObject, TEXT("node_title"));
					if (!NodeTitle.IsEmpty())
					{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
						Node->SetNodeTitle(FName(*NodeTitle));
#else
						ResultObject->SetStringField(TEXT("node_title_warning"), TEXT("node_title is ignored on UE versions where UPCGNode::SetNodeTitle is unavailable"));
#endif
					}

					int32 PositionX = 0;
					int32 PositionY = 0;
					if ((*OperationObject)->TryGetNumberField(TEXT("position_x"), PositionX) || (*OperationObject)->TryGetNumberField(TEXT("position_y"), PositionY))
					{
						Node->SetNodePosition(PositionX, PositionY);
					}

					const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
					if (Settings && (*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && PropertiesObject->IsValid())
					{
						if (!ApplyPropertyMap(Settings, *PropertiesObject, true, OperationError))
						{
							Graph->RemoveNode(Node);
							Node = nullptr;
						}
					}
					if (Node)
					{
						ResultObject->SetObjectField(TEXT("node"), SerializePCGNode(Node, Graph->GetNodes().Find(Node)));
						bOperationSuccess = true;
					}
				}
			}
		}
		else if (Action == TEXT("remove_node"))
		{
			UPCGNode* Node = ResolveNode(Graph, *OperationObject);
			if (!Node)
			{
				OperationError = TEXT("Node not found");
			}
			else
			{
				if (!bDryRun)
				{
					Graph->RemoveNode(Node);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("connect"))
		{
			UPCGNode* FromNode = ResolveNode(Graph, *OperationObject, TEXT("from_node"));
			UPCGNode* ToNode = ResolveNode(Graph, *OperationObject, TEXT("to_node"));
			const FName FromPin(*GetStringArgOrDefault(*OperationObject, TEXT("from_pin"), TEXT("Out")));
			const FName ToPin(*GetStringArgOrDefault(*OperationObject, TEXT("to_pin"), TEXT("In")));
			if (!FromNode || !ToNode)
			{
				OperationError = TEXT("from_node and to_node references must resolve");
			}
			else if (!HasPinWithLabel(FromNode, FromPin, true))
			{
				OperationError = FString::Printf(TEXT("Output pin '%s' was not found on from_node"), *FromPin.ToString());
			}
			else if (!HasPinWithLabel(ToNode, ToPin, false))
			{
				OperationError = FString::Printf(TEXT("Input pin '%s' was not found on to_node"), *ToPin.ToString());
			}
			else
			{
				if (!bDryRun && !Graph->AddLabeledEdge(FromNode, FromPin, ToNode, ToPin))
				{
					OperationError = TEXT("PCG graph rejected the edge");
				}
				else
				{
					bOperationSuccess = true;
				}
			}
		}
		else if (Action == TEXT("set_node_properties"))
		{
			UPCGNode* Node = ResolveNode(Graph, *OperationObject);
			const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
			if (!(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !PropertiesObject->IsValid())
			{
				OperationError = TEXT("'properties' object is required");
			}
			else if (!Node)
			{
				OperationError = TEXT("Node not found");
			}
			else if (!Node->GetSettings())
			{
				OperationError = TEXT("Node does not expose settings");
			}
			else if (!ApplyPropertyMap(Node->GetSettings(), *PropertiesObject, !bDryRun, OperationError))
			{
				// OperationError is populated.
			}
			else
			{
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("set_graph_properties"))
		{
			const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
			if (!(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !PropertiesObject->IsValid())
			{
				OperationError = TEXT("'properties' object is required");
			}
			else if (!GraphInterface)
			{
				OperationError = TEXT("Graph asset is not available for property validation");
			}
			else if (!ApplyPropertyMap(GraphInterface, *PropertiesObject, !bDryRun, OperationError))
			{
				// OperationError is populated.
			}
			else
			{
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

	bool bRolledBack = false;
	if (!bDryRun && bAnyFailed && bRollbackOnError)
	{
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
		}
		bRolledBack = true;
		bChanged = false;
	}

	if (!bDryRun && bChanged && GraphInterface && (!bAnyFailed || !bRollbackOnError))
	{
		FMcpAssetModifier::MarkPackageDirty(GraphInterface);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(GraphInterface, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = BuildGraphSummary(AssetPath, GraphInterface);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetBoolField(TEXT("rollback_on_error"), bRollbackOnError);
	Response->SetBoolField(TEXT("rolled_back"), bRolledBack);
	Response->SetBoolField(TEXT("graph_created"), bGraphCreated);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

FString URunPCGGraphTool::GetToolDescription() const
{
	return TEXT("Bind an optional PCG graph to an actor PCG component and trigger generate or cleanup.");
}

TMap<FString, FMcpSchemaProperty> URunPCGGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label containing a PCG component"), true));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional PCG component name")));
	Schema.Add(TEXT("graph_asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional PCG graph asset to bind before running")));
	Schema.Add(TEXT("action"), FMcpSchemaProperty::MakeEnum(TEXT("Run action"), { TEXT("generate"), TEXT("cleanup") }));
	Schema.Add(TEXT("force"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Force PCG generation. Default: true.")));
	Schema.Add(TEXT("remove_components"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Remove generated components during cleanup. Default: true.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without running PCG")));
	return Schema;
}

FMcpToolResult URunPCGGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const FString GraphAssetPath = GetStringArgOrDefault(Arguments, TEXT("graph_asset_path"));
	const FString Action = GetStringArgOrDefault(Arguments, TEXT("action"), TEXT("generate")).ToLower();
	const bool bForce = GetBoolArgOrDefault(Arguments, TEXT("force"), true);
	const bool bRemoveComponents = GetBoolArgOrDefault(Arguments, TEXT("remove_components"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	UPCGComponent* Component = nullptr;
	TArray<UPCGComponent*> Components;
	Actor->GetComponents(Components);
	for (UPCGComponent* Candidate : Components)
	{
		if (Candidate && (ComponentName.IsEmpty() || Candidate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
		{
			Component = Candidate;
			break;
		}
	}
	if (!Component)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), TEXT("No matching PCG component found on actor"));
	}

	UPCGGraphInterface* GraphInterface = nullptr;
	if (!GraphAssetPath.IsEmpty())
	{
		FString LoadError;
		GraphInterface = FMcpAssetModifier::LoadAssetByPath<UPCGGraphInterface>(GraphAssetPath, LoadError);
		if (!GraphInterface)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}
	}

	if (!bDryRun)
	{
		Component->Modify();
		if (GraphInterface)
		{
			Component->SetGraphLocal(GraphInterface);
			Component->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
			Component->bActivated = true;
		}

		if (Action == TEXT("generate"))
		{
			Component->GenerateLocal(bForce);
		}
		else if (Action == TEXT("cleanup"))
		{
			Component->CleanupLocal(bRemoveComponents);
		}
		else
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'action' must be 'generate' or 'cleanup'"));
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("world_name"), World->GetName());
	Response->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
	Response->SetStringField(TEXT("component_name"), Component->GetName());
	Response->SetStringField(TEXT("action"), Action);
	Response->SetStringField(TEXT("bound_graph_asset_path"), GraphAssetPath);
	Response->SetBoolField(TEXT("has_graph"), Component->GetGraph() != nullptr);
	return FMcpToolResult::StructuredJson(Response);
}
