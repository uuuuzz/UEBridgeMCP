// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/EditBlueprintGraphTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Blueprint/BlueprintToolUtils.h"
#include "Tools/Write/AddGraphNodeTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "Utils/McpV2ToolUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "Engine/World.h"
#include "K2Node.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Select.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "EdGraphNode_Comment.h"

namespace
{
	TSharedPtr<FMcpSchemaProperty> MakeOpenObjectSchema(const FString& Description)
	{
		TSharedPtr<FJsonObject> RawSchema = MakeShareable(new FJsonObject);
		RawSchema->SetStringField(TEXT("type"), TEXT("object"));
		RawSchema->SetBoolField(TEXT("additionalProperties"), true);

		TSharedPtr<FMcpSchemaProperty> Schema = MakeShared<FMcpSchemaProperty>();
		Schema->Description = Description;
		Schema->RawSchema = RawSchema;
		return Schema;
	}

	TSharedPtr<FMcpSchemaProperty> MakeStringArraySchema(const FString& Description)
	{
		TSharedPtr<FMcpSchemaProperty> Schema = MakeShared<FMcpSchemaProperty>();
		Schema->Type = TEXT("array");
		Schema->ItemsType = TEXT("string");
		Schema->Description = Description;
		return Schema;
	}

	TSharedPtr<FMcpSchemaProperty> MakeNumberArraySchema(const FString& Description)
	{
		TSharedPtr<FMcpSchemaProperty> Schema = MakeShared<FMcpSchemaProperty>();
		Schema->Type = TEXT("array");
		Schema->ItemsType = TEXT("number");
		Schema->Description = Description;
		return Schema;
	}

	FString JsonValueToLiteral(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->IsNull())
		{
			return TEXT("");
		}

		switch (Value->Type)
		{
		case EJson::String:
			return Value->AsString();
		case EJson::Number:
			return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		default:
			{
				FString Serialized;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
				FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT("value"), Writer);
				return Serialized;
			}
		}
	}

	bool ResolveNodeToken(
		const FString& Token,
		const TMap<FString, FGuid>& AliasMap,
		FGuid& OutGuid,
		FString& OutError)
	{
		if (Token.IsEmpty())
		{
			OutError = TEXT("Node reference is empty");
			return false;
		}

		if (const FGuid* AliasGuid = AliasMap.Find(Token))
		{
			OutGuid = *AliasGuid;
			return true;
		}

		if (FGuid::Parse(Token, OutGuid))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown node reference or invalid GUID: %s"), *Token);
		return false;
	}

	UEdGraphNode* ResolveNode(
		UBlueprint* Blueprint,
		const TMap<FString, FGuid>& AliasMap,
		const FString& Token,
		UEdGraph** OutGraph,
		FString& OutError)
	{
		FGuid NodeGuid;
		if (!ResolveNodeToken(Token, AliasMap, NodeGuid, OutError))
		{
			return nullptr;
		}

		UEdGraphNode* Node = FMcpAssetModifier::FindNodeByGuid(Blueprint, NodeGuid, OutGraph);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("Node not found: %s"), *Token);
		}
		return Node;
	}

	bool TryGetObjectField(
		const TSharedPtr<FJsonObject>& Object,
		const FString& FieldName,
		TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
		if (Object->TryGetObjectField(FieldName, ObjectPtr) && ObjectPtr && (*ObjectPtr).IsValid())
		{
			OutObject = *ObjectPtr;
			return true;
		}
		return false;
	}

	bool TryParsePositionField(
		const TSharedPtr<FJsonObject>& Operation,
		FVector2D& OutPosition,
		FString& OutError)
	{
		if (!Operation->HasField(TEXT("position")))
		{
			OutPosition = FVector2D::ZeroVector;
			return true;
		}

		return BlueprintToolUtils::TryParsePosition(Operation, TEXT("position"), OutPosition, OutError);
	}

	bool TryParseDeltaField(
		const TSharedPtr<FJsonObject>& Operation,
		FVector2D& OutDelta,
		FString& OutError)
	{
		return BlueprintToolUtils::TryParsePosition(Operation, TEXT("delta"), OutDelta, OutError);
	}

	bool TryParseColorField(
		const TSharedPtr<FJsonObject>& Operation,
		const FString& FieldName,
		FLinearColor& OutColor)
	{
		OutColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);

		const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
		if (!Operation->TryGetArrayField(FieldName, ColorArray) || !ColorArray || ColorArray->Num() < 3)
		{
			return false;
		}

		OutColor.R = static_cast<float>((*ColorArray)[0]->AsNumber());
		OutColor.G = static_cast<float>((*ColorArray)[1]->AsNumber());
		OutColor.B = static_cast<float>((*ColorArray)[2]->AsNumber());
		OutColor.A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;
		return true;
	}

	void MarkBlueprintGraphDirty(UBlueprint* Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FMcpAssetModifier::MarkPackageDirty(Blueprint);
	}

	UEdGraphNode* CreateRawGraphNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		UClass* NodeClass,
		const FVector2D& Position,
		FString& OutError)
	{
		if (!Blueprint || !Graph || !NodeClass)
		{
			OutError = TEXT("Blueprint, graph, or node class is null");
			return nullptr;
		}

		FMcpAssetModifier::MarkModified(Blueprint);
		Graph->Modify();

		UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		if (!NewNode)
		{
			OutError = FString::Printf(TEXT("Failed to create node of class %s"), *NodeClass->GetName());
			return nullptr;
		}

		Graph->AddNode(NewNode, true, false);
		NewNode->CreateNewGuid();
		NewNode->NodePosX = static_cast<int32>(Position.X);
		NewNode->NodePosY = static_cast<int32>(Position.Y);
		NewNode->PostPlacedNewNode();
		return NewNode;
	}

	void AddNodeResultFields(
		const FString& SessionId,
		const FString& AssetPath,
		UEdGraphNode* Node,
		TSharedPtr<FJsonObject>& ResultObject,
		const FString& Alias)
	{
		if (!Node)
		{
			return;
		}

		if (!Alias.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("alias"), Alias);
		}
		ResultObject->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		ResultObject->SetObjectField(
			TEXT("handle"),
			McpV2ToolUtils::MakeEntityHandle(
				TEXT("blueprint_node"),
				SessionId,
				AssetPath,
				Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
				Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		ResultObject->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	}

	bool FinalizeSpecializedNode(
		UBlueprint* Blueprint,
		UEdGraphNode* Node,
		const TSharedPtr<FJsonObject>& PinDefaults,
		FString& OutError)
	{
		if (!Blueprint || !Node)
		{
			OutError = TEXT("Blueprint or node is null");
			return false;
		}

		if (UK2Node* K2Node = Cast<UK2Node>(Node))
		{
			K2Node->AllocateDefaultPins();
		}
		else
		{
			Node->AllocateDefaultPins();
		}

		if (PinDefaults.IsValid())
		{
			UAddGraphNodeTool::ApplyPinDefaults(Node, PinDefaults);
		}

		MarkBlueprintGraphDirty(Blueprint);
		return true;
	}

	TSharedPtr<FJsonObject> MakeSummaryObject(int32 TotalOperations, int32 FailedOperations, bool bCompiled, bool bSaved)
	{
		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetNumberField(TEXT("total_operations"), TotalOperations);
		Summary->SetNumberField(TEXT("failed_operations"), FailedOperations);
		Summary->SetBoolField(TEXT("compiled"), bCompiled);
		Summary->SetBoolField(TEXT("saved"), bSaved);
		return Summary;
	}

	bool CollectOperationNodes(
		UBlueprint* Blueprint,
		const TSharedPtr<FJsonObject>& Operation,
		const TMap<FString, FGuid>& AliasMap,
		TArray<UEdGraphNode*>& OutNodes,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
		if (!Operation->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray || NodesArray->Num() == 0)
		{
			OutError = TEXT("'nodes' array is required");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
		{
			FString NodeToken;
			if (!NodeValue.IsValid() || !NodeValue->TryGetString(NodeToken))
			{
				OutError = TEXT("Each entry in 'nodes' must be a string");
				return false;
			}

			UEdGraphNode* Node = ResolveNode(Blueprint, AliasMap, NodeToken, nullptr, OutError);
			if (!Node)
			{
				return false;
			}

			OutNodes.Add(Node);
		}

		return true;
	}
}

FString UEditBlueprintGraphTool::GetToolDescription() const
{
	return TEXT("Transactional Blueprint graph editing with batched operations, alias support, Blueprint-specific node factories, dry-run validation, and optional compile/save.");
}

TMap<FString, FMcpSchemaProperty> UEditBlueprintGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> PositionSchema = MakeNumberArraySchema(TEXT("2D vector [x, y]"));
	TSharedPtr<FMcpSchemaProperty> GenericObjectSchema = MakeOpenObjectSchema(TEXT("Open-ended property map"));
	TSharedPtr<FMcpSchemaProperty> NodeRefsSchema = MakeStringArraySchema(TEXT("Node references as aliases or GUIDs"));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->NestedRequired = {TEXT("op")};
	OperationSchema->Properties.Add(TEXT("op"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Graph operation"),
		{
			TEXT("add_node"),
			TEXT("remove_node"),
			TEXT("connect"),
			TEXT("disconnect"),
			TEXT("set_pin_default"),
			TEXT("move_node"),
			TEXT("set_node_property"),
			TEXT("create_custom_event"),
			TEXT("create_function_graph"),
			TEXT("create_macro_graph"),
			TEXT("add_reroute"),
			TEXT("add_comment"),
			TEXT("add_branch"),
			TEXT("add_sequence"),
			TEXT("add_for_each_loop"),
			TEXT("add_switch_on_enum"),
			TEXT("add_select"),
			TEXT("add_make_array"),
			TEXT("add_break_struct"),
			TEXT("add_make_struct"),
			TEXT("add_call_function"),
			TEXT("add_get_variable"),
			TEXT("add_set_variable"),
			TEXT("add_cast"),
			TEXT("add_spawn_actor"),
			TEXT("add_create_widget"),
			TEXT("auto_connect"),
			TEXT("move_nodes_batch"),
			TEXT("comment_region"),
			TEXT("layout_graph")
		},
		true)));
	OperationSchema->Properties.Add(TEXT("alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Alias for newly created nodes"))));
	OperationSchema->Properties.Add(TEXT("graph_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target graph name"))));
	OperationSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Operation-specific name"))));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Operation-specific new name"))));
	OperationSchema->Properties.Add(TEXT("function_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Function name or local-variable scope graph"))));
	OperationSchema->Properties.Add(TEXT("function_class_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Owning class path for add_call_function"))));
	OperationSchema->Properties.Add(TEXT("variable_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Variable name for get/set variable nodes"))));
	OperationSchema->Properties.Add(TEXT("node_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Node class for add_node"))));
	OperationSchema->Properties.Add(TEXT("node"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing node GUID or alias"))));
	OperationSchema->Properties.Add(TEXT("source_node"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source node GUID or alias"))));
	OperationSchema->Properties.Add(TEXT("source_pin"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source pin name"))));
	OperationSchema->Properties.Add(TEXT("target_node"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target node GUID or alias"))));
	OperationSchema->Properties.Add(TEXT("target_pin"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target pin name"))));
	OperationSchema->Properties.Add(TEXT("pin_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Pin name for set_pin_default/disconnect"))));
	OperationSchema->Properties.Add(TEXT("position"), PositionSchema);
	OperationSchema->Properties.Add(TEXT("delta"), PositionSchema);
	OperationSchema->Properties.Add(TEXT("size"), PositionSchema);
	OperationSchema->Properties.Add(TEXT("properties"), GenericObjectSchema);
	OperationSchema->Properties.Add(TEXT("pin_defaults"), GenericObjectSchema);
	OperationSchema->Properties.Add(TEXT("property_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Property path for set_node_property"))));
	OperationSchema->Properties.Add(TEXT("class_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Class path for cast/spawn/widget/class-sensitive operations"))));
	OperationSchema->Properties.Add(TEXT("enum_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Enum path for enum-sensitive operations"))));
	OperationSchema->Properties.Add(TEXT("struct_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Struct path for struct-sensitive operations"))));
	OperationSchema->Properties.Add(TEXT("comment_text"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Comment text"))));
	OperationSchema->Properties.Add(TEXT("comment_color"), PositionSchema);
	OperationSchema->Properties.Add(TEXT("nodes"), NodeRefsSchema);
	OperationSchema->Properties.Add(TEXT("num_inputs"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Requested number of input pins"))));
	OperationSchema->Properties.Add(TEXT("option_count"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Requested number of select options"))));
	OperationSchema->Properties.Add(TEXT("spacing_x"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Horizontal layout spacing"))));
	OperationSchema->Properties.Add(TEXT("spacing_y"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Vertical layout spacing"))));
	OperationSchema->Properties.Add(TEXT("start_x"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Layout start X"))));
	OperationSchema->Properties.Add(TEXT("start_y"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Layout start Y"))));
	OperationSchema->Properties.Add(TEXT("call_in_editor"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Call-in-editor flag for custom events"))));
	OperationSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("any"), TEXT("Value payload for pin/property edits"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.Description = TEXT("Batched graph operations");
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate by applying edits on a transient Blueprint copy")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on first error")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile after apply")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after apply")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transaction label")));
	return Schema;
}

TArray<FString> UEditBlueprintGraphTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("operations")};
}

FMcpToolResult UEditBlueprintGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Blueprint Graph"));

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), OperationsArray) || !OperationsArray || OperationsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
		FMcpAssetModifier::MarkModified(Blueprint);
	}

	UBlueprint* WorkingBlueprint = Blueprint;
	if (bDryRun)
	{
		WorkingBlueprint = DuplicateObject<UBlueprint>(Blueprint, GetTransientPackage());
		if (!WorkingBlueprint)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), TEXT("Failed to create dry-run Blueprint copy"));
		}
	}

	TMap<FString, FGuid> AliasMap;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bMutatedBlueprint = false;

	for (int32 Index = 0; Index < OperationsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*OperationsArray)[Index]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			TSharedPtr<FJsonObject> InvalidResult = MakeShareable(new FJsonObject);
			InvalidResult->SetNumberField(TEXT("index"), Index);
			InvalidResult->SetStringField(TEXT("op"), TEXT("unknown"));
			InvalidResult->SetBoolField(TEXT("success"), false);
			InvalidResult->SetStringField(TEXT("error"), TEXT("Invalid operation object"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(InvalidResult)));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(InvalidResult)));
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("Invalid operation object"));
			}
			continue;
		}

		const TSharedPtr<FJsonObject>& Operation = *OperationObject;
		const FString Op = GetStringArgOrDefault(Operation, TEXT("op"));
		const FString Alias = GetStringArgOrDefault(Operation, TEXT("alias"));
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetStringField(TEXT("op"), Op);
		ResultObject->SetNumberField(TEXT("index"), Index);

		FString ErrorMessage;
		bool bSuccess = false;

		auto RecordCreatedNode = [&](UEdGraphNode* Node)
		{
			if (!Node)
			{
				return;
			}

			if (!Alias.IsEmpty())
			{
				AliasMap.Add(Alias, Node->NodeGuid);
			}

			if (!bDryRun)
			{
				FMcpEditorSessionManager::Get().RememberBlueprintNode(Context.SessionId, AssetPath, Node);
			}

			AddNodeResultFields(Context.SessionId, AssetPath, Node, ResultObject, Alias);
		};

		if (Op == TEXT("add_node"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString NodeClass = GetStringArgOrDefault(Operation, TEXT("node_class"));
			FVector2D Position;
			if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else if (NodeClass.IsEmpty())
			{
				ErrorMessage = TEXT("'node_class' is required for add_node");
			}
			else
			{
				UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName);
				if (!TargetGraph)
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
				else
				{
					TSharedPtr<FJsonObject> Properties;
					TSharedPtr<FJsonObject> PinDefaults;
					TryGetObjectField(Operation, TEXT("properties"), Properties);
					TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);

					UEdGraphNode* NewNode = UAddGraphNodeTool::CreateBlueprintNode(WorkingBlueprint, TargetGraph, NodeClass, Position, Properties, PinDefaults, ErrorMessage);
					if (NewNode)
					{
						RecordCreatedNode(NewNode);
						bSuccess = true;
						bMutatedBlueprint = true;
					}
				}
			}
		}
		else if (Op == TEXT("create_function_graph"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("name"));
			if (GraphName.IsEmpty())
			{
				ErrorMessage = TEXT("'name' is required for create_function_graph");
			}
			else if (FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				ErrorMessage = FString::Printf(TEXT("Graph '%s' already exists"), *GraphName);
			}
			else
			{
				UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
					WorkingBlueprint,
					FName(*GraphName),
					UEdGraph::StaticClass(),
					UEdGraphSchema_K2::StaticClass());

				if (!NewGraph)
				{
					ErrorMessage = FString::Printf(TEXT("Failed to create function graph '%s'"), *GraphName);
				}
				else
				{
					FBlueprintEditorUtils::AddFunctionGraph<UClass>(WorkingBlueprint, NewGraph, true, nullptr);
					MarkBlueprintGraphDirty(WorkingBlueprint);
					ResultObject->SetStringField(TEXT("graph_name"), NewGraph->GetName());
					bSuccess = true;
					bMutatedBlueprint = true;
				}
			}
		}
		else if (Op == TEXT("create_macro_graph"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("name"));
			if (GraphName.IsEmpty())
			{
				ErrorMessage = TEXT("'name' is required for create_macro_graph");
			}
			else if (FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				ErrorMessage = FString::Printf(TEXT("Graph '%s' already exists"), *GraphName);
			}
			else
			{
				UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
					WorkingBlueprint,
					FName(*GraphName),
					UEdGraph::StaticClass(),
					UEdGraphSchema_K2::StaticClass());

				if (!NewGraph)
				{
					ErrorMessage = FString::Printf(TEXT("Failed to create macro graph '%s'"), *GraphName);
				}
				else
				{
					FBlueprintEditorUtils::AddMacroGraph(WorkingBlueprint, NewGraph, true, nullptr);
					MarkBlueprintGraphDirty(WorkingBlueprint);
					ResultObject->SetStringField(TEXT("graph_name"), NewGraph->GetName());
					bSuccess = true;
					bMutatedBlueprint = true;
				}
			}
		}
		else if (Op == TEXT("create_custom_event"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString EventName = GetStringArgOrDefault(Operation, TEXT("name"));
			FVector2D Position;
			if (EventName.IsEmpty())
			{
				ErrorMessage = TEXT("'name' is required for create_custom_event");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName);
				if (!TargetGraph)
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
				else
				{
					for (UEdGraphNode* ExistingNode : TargetGraph->Nodes)
					{
						if (const UK2Node_CustomEvent* ExistingEvent = Cast<UK2Node_CustomEvent>(ExistingNode))
						{
							if (ExistingEvent->CustomFunctionName.ToString().Equals(EventName, ESearchCase::IgnoreCase))
							{
								ErrorMessage = FString::Printf(TEXT("Custom event '%s' already exists"), *EventName);
								break;
							}
						}
					}
				}

				if (ErrorMessage.IsEmpty())
				{
					UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(CreateRawGraphNode(
						WorkingBlueprint,
						TargetGraph,
						UK2Node_CustomEvent::StaticClass(),
						Position,
						ErrorMessage));
					if (EventNode)
					{
						EventNode->CustomFunctionName = FName(*EventName);
						EventNode->bCallInEditor = GetBoolArgOrDefault(Operation, TEXT("call_in_editor"), false);

						if (!FinalizeSpecializedNode(WorkingBlueprint, EventNode, nullptr, ErrorMessage))
						{
							TargetGraph->RemoveNode(EventNode);
						}
						else
						{
							const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
							if (Operation->TryGetArrayField(TEXT("inputs"), InputsArray))
							{
								bSuccess = BlueprintToolUtils::SynchronizeUserDefinedPins(EventNode, InputsArray, EGPD_Output, ErrorMessage);
							}
							else
							{
								bSuccess = true;
							}

							if (bSuccess)
							{
								FKismetUserDeclaredFunctionMetadata& MetaData = EventNode->GetUserDefinedMetaData();
								const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
								if (Operation->TryGetObjectField(TEXT("metadata"), MetadataObject) && MetadataObject && (*MetadataObject).IsValid())
								{
									bSuccess = BlueprintToolUtils::ApplyFunctionMetadataObject(MetaData, *MetadataObject, ErrorMessage);
								}

								FString Tooltip;
								if (bSuccess && Operation->TryGetStringField(TEXT("tooltip"), Tooltip))
								{
									MetaData.ToolTip = FText::FromString(Tooltip);
								}
							}

							if (bSuccess)
							{
								RecordCreatedNode(EventNode);
								ResultObject->SetStringField(TEXT("graph_name"), GraphName);
								bMutatedBlueprint = true;
							}
							else
							{
								TargetGraph->RemoveNode(EventNode);
							}
						}
					}
				}
			}
		}
		else if (Op == TEXT("add_reroute")
			|| Op == TEXT("add_branch")
			|| Op == TEXT("add_sequence")
			|| Op == TEXT("add_comment"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			FVector2D Position;
			if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName);
				if (!TargetGraph)
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
				else
				{
					const FString NodeClass =
						Op == TEXT("add_reroute") ? TEXT("K2Node_Knot") :
						Op == TEXT("add_branch") ? TEXT("K2Node_IfThenElse") :
						Op == TEXT("add_sequence") ? TEXT("K2Node_ExecutionSequence") :
						TEXT("EdGraphNode_Comment");

					TSharedPtr<FJsonObject> Properties;
					TSharedPtr<FJsonObject> PinDefaults;
					TryGetObjectField(Operation, TEXT("properties"), Properties);
					TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);

					if (Op == TEXT("add_comment"))
					{
						if (!Properties.IsValid())
						{
							Properties = MakeShareable(new FJsonObject);
						}

						FString CommentText = GetStringArgOrDefault(Operation, TEXT("comment_text"));
						if (!CommentText.IsEmpty())
						{
							Properties->SetStringField(TEXT("NodeComment"), CommentText);
						}
					}

					UEdGraphNode* NewNode = UAddGraphNodeTool::CreateBlueprintNode(WorkingBlueprint, TargetGraph, NodeClass, Position, Properties, PinDefaults, ErrorMessage);
					if (NewNode)
					{
						if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(NewNode))
						{
							FVector2D Size(400.0f, 220.0f);
							if (Operation->HasField(TEXT("size")))
							{
								FString SizeError;
								if (!BlueprintToolUtils::TryParsePosition(Operation, TEXT("size"), Size, SizeError))
								{
									ErrorMessage = SizeError;
									TargetGraph->RemoveNode(CommentNode);
								}
							}

							if (ErrorMessage.IsEmpty())
							{
								FLinearColor CommentColor;
								if (TryParseColorField(Operation, TEXT("comment_color"), CommentColor))
								{
									CommentNode->CommentColor = CommentColor;
								}
								CommentNode->SetBounds(FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y));
							}
						}

						if (ErrorMessage.IsEmpty())
						{
							RecordCreatedNode(NewNode);
							ResultObject->SetStringField(TEXT("graph_name"), GraphName);
							bSuccess = true;
							bMutatedBlueprint = true;
						}
					}
				}
			}
		}
		else if (Op == TEXT("add_for_each_loop"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			FVector2D Position;
			if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName);
				if (!TargetGraph)
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
				else
				{
					FString MacroError;
					UEdGraph* MacroGraph = BlueprintToolUtils::LoadStandardMacroGraph(
						GetStringArgOrDefault(Operation, TEXT("name"), TEXT("ForEachLoop")),
						MacroError);
					if (!MacroGraph)
					{
						ErrorMessage = MacroError;
					}
					else
					{
						UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(CreateRawGraphNode(
							WorkingBlueprint,
							TargetGraph,
							UK2Node_MacroInstance::StaticClass(),
							Position,
							ErrorMessage));
						if (MacroNode)
						{
							MacroNode->SetMacroGraph(MacroGraph);
							if (FinalizeSpecializedNode(WorkingBlueprint, MacroNode, nullptr, ErrorMessage))
							{
								RecordCreatedNode(MacroNode);
								ResultObject->SetStringField(TEXT("graph_name"), GraphName);
								bSuccess = true;
								bMutatedBlueprint = true;
							}
							else
							{
								TargetGraph->RemoveNode(MacroNode);
							}
						}
					}
				}
			}
		}
		else if (Op == TEXT("add_switch_on_enum"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString EnumPath = GetStringArgOrDefault(Operation, TEXT("enum_path"));
			FVector2D Position;
			if (EnumPath.IsEmpty())
			{
				ErrorMessage = TEXT("'enum_path' is required for add_switch_on_enum");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				FString EnumError;
				UEnum* Enum = BlueprintToolUtils::ResolveEnum(EnumPath, EnumError);
				if (!Enum)
				{
					ErrorMessage = EnumError;
				}
				else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
				{
					UK2Node_SwitchEnum* SwitchNode = Cast<UK2Node_SwitchEnum>(CreateRawGraphNode(
						WorkingBlueprint,
						TargetGraph,
						UK2Node_SwitchEnum::StaticClass(),
						Position,
						ErrorMessage));
					if (SwitchNode)
					{
						SwitchNode->SetEnum(Enum);
						if (FinalizeSpecializedNode(WorkingBlueprint, SwitchNode, nullptr, ErrorMessage))
						{
							RecordCreatedNode(SwitchNode);
							ResultObject->SetStringField(TEXT("graph_name"), GraphName);
							bSuccess = true;
							bMutatedBlueprint = true;
						}
						else
						{
							TargetGraph->RemoveNode(SwitchNode);
						}
					}
				}
				else
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
			}
		}
		else if (Op == TEXT("add_select"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString EnumPath = GetStringArgOrDefault(Operation, TEXT("enum_path"));
			const int32 OptionCount = FMath::Max(2, GetIntArgOrDefault(Operation, TEXT("option_count"), 2));
			FVector2D Position;
			if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				UK2Node_Select* SelectNode = Cast<UK2Node_Select>(CreateRawGraphNode(
					WorkingBlueprint,
					TargetGraph,
					UK2Node_Select::StaticClass(),
					Position,
					ErrorMessage));
				if (SelectNode)
				{
					if (!EnumPath.IsEmpty())
					{
						FString EnumError;
						if (UEnum* Enum = BlueprintToolUtils::ResolveEnum(EnumPath, EnumError))
						{
							SelectNode->SetEnum(Enum, true);
						}
						else
						{
							ErrorMessage = EnumError;
						}
					}

					if (ErrorMessage.IsEmpty() && FinalizeSpecializedNode(WorkingBlueprint, SelectNode, nullptr, ErrorMessage))
					{
						TArray<UEdGraphPin*> OptionPins;
						SelectNode->GetOptionPins(OptionPins);
						IK2Node_AddPinInterface* AddPinInterface = static_cast<IK2Node_AddPinInterface*>(SelectNode);
						while (OptionPins.Num() < OptionCount && AddPinInterface && AddPinInterface->CanAddPin())
						{
							AddPinInterface->AddInputPin();
							OptionPins.Reset();
							SelectNode->GetOptionPins(OptionPins);
						}

						TSharedPtr<FJsonObject> PinDefaults;
						TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);
						if (PinDefaults.IsValid())
						{
							UAddGraphNodeTool::ApplyPinDefaults(SelectNode, PinDefaults);
						}

						RecordCreatedNode(SelectNode);
						ResultObject->SetStringField(TEXT("graph_name"), GraphName);
						bSuccess = true;
						bMutatedBlueprint = true;
					}
					else
					{
						TargetGraph->RemoveNode(SelectNode);
					}
				}
			}
			else
			{
				ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			}
		}
		else if (Op == TEXT("add_make_array"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const int32 NumInputs = FMath::Max(1, GetIntArgOrDefault(Operation, TEXT("num_inputs"), 1));
			FVector2D Position;
			if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				UK2Node_MakeArray* ArrayNode = Cast<UK2Node_MakeArray>(CreateRawGraphNode(
					WorkingBlueprint,
					TargetGraph,
					UK2Node_MakeArray::StaticClass(),
					Position,
					ErrorMessage));
				if (ArrayNode && FinalizeSpecializedNode(WorkingBlueprint, ArrayNode, nullptr, ErrorMessage))
				{
					while (ArrayNode->NumInputs < NumInputs)
					{
						ArrayNode->AddInputPin();
					}

					TSharedPtr<FJsonObject> PinDefaults;
					TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);
					if (PinDefaults.IsValid())
					{
						UAddGraphNodeTool::ApplyPinDefaults(ArrayNode, PinDefaults);
					}

					RecordCreatedNode(ArrayNode);
					ResultObject->SetStringField(TEXT("graph_name"), GraphName);
					bSuccess = true;
					bMutatedBlueprint = true;
				}
				else if (ArrayNode)
				{
					TargetGraph->RemoveNode(ArrayNode);
				}
			}
			else
			{
				ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			}
		}
		else if (Op == TEXT("add_break_struct") || Op == TEXT("add_make_struct"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString StructPath = GetStringArgOrDefault(Operation, TEXT("struct_path"));
			FVector2D Position;
			if (StructPath.IsEmpty())
			{
				ErrorMessage = TEXT("'struct_path' is required");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				FString StructError;
				UScriptStruct* Struct = BlueprintToolUtils::ResolveStruct(StructPath, StructError);
				if (!Struct)
				{
					ErrorMessage = StructError;
				}
				else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
				{
					UClass* NodeClass = (Op == TEXT("add_make_struct")) ? UK2Node_MakeStruct::StaticClass() : UK2Node_BreakStruct::StaticClass();
					UEdGraphNode* NewNode = CreateRawGraphNode(WorkingBlueprint, TargetGraph, NodeClass, Position, ErrorMessage);
					if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(NewNode))
					{
						MakeStructNode->StructType = Struct;
					}
					else if (UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(NewNode))
					{
						BreakStructNode->StructType = Struct;
					}

					if (NewNode && FinalizeSpecializedNode(WorkingBlueprint, NewNode, nullptr, ErrorMessage))
					{
						RecordCreatedNode(NewNode);
						ResultObject->SetStringField(TEXT("graph_name"), GraphName);
						bSuccess = true;
						bMutatedBlueprint = true;
					}
					else if (NewNode)
					{
						TargetGraph->RemoveNode(NewNode);
					}
				}
				else
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
			}
		}
		else if (Op == TEXT("add_call_function"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString FunctionName = GetStringArgOrDefault(Operation, TEXT("function_name"), GetStringArgOrDefault(Operation, TEXT("name")));
			const FString FunctionClassPath = GetStringArgOrDefault(Operation, TEXT("function_class_path"), GetStringArgOrDefault(Operation, TEXT("class_path")));
			FVector2D Position;
			if (FunctionName.IsEmpty())
			{
				ErrorMessage = TEXT("'function_name' or 'name' is required for add_call_function");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				FString FunctionError;
				UFunction* Function = BlueprintToolUtils::ResolveFunction(WorkingBlueprint, FunctionClassPath, FunctionName, FunctionError);
				if (!Function)
				{
					ErrorMessage = FunctionError;
				}
				else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
				{
					UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(CreateRawGraphNode(
						WorkingBlueprint,
						TargetGraph,
						UK2Node_CallFunction::StaticClass(),
						Position,
						ErrorMessage));
					if (CallNode)
					{
						CallNode->SetFromFunction(Function);
						TSharedPtr<FJsonObject> PinDefaults;
						TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);
						if (FinalizeSpecializedNode(WorkingBlueprint, CallNode, PinDefaults, ErrorMessage))
						{
							RecordCreatedNode(CallNode);
							ResultObject->SetStringField(TEXT("graph_name"), GraphName);
							bSuccess = true;
							bMutatedBlueprint = true;
						}
						else
						{
							TargetGraph->RemoveNode(CallNode);
						}
					}
				}
				else
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
			}
		}
		else if (Op == TEXT("add_get_variable") || Op == TEXT("add_set_variable"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString ScopeFunctionName = GetStringArgOrDefault(Operation, TEXT("function_name"));
			const FString VariableName = GetStringArgOrDefault(Operation, TEXT("variable_name"), GetStringArgOrDefault(Operation, TEXT("name")));
			FVector2D Position;
			if (VariableName.IsEmpty())
			{
				ErrorMessage = TEXT("'variable_name' or 'name' is required");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				UClass* NodeClass = (Op == TEXT("add_get_variable")) ? UK2Node_VariableGet::StaticClass() : UK2Node_VariableSet::StaticClass();
				UEdGraphNode* NewNode = CreateRawGraphNode(WorkingBlueprint, TargetGraph, NodeClass, Position, ErrorMessage);
				if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(NewNode))
				{
					const FName VariableFName(*VariableName);
					if (!ScopeFunctionName.IsEmpty())
					{
						UEdGraph* ScopeGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, ScopeFunctionName);
						if (!ScopeGraph)
						{
							ErrorMessage = FString::Printf(TEXT("Function '%s' not found"), *ScopeFunctionName);
						}
						else
						{
							const FGuid LocalGuid = FBlueprintEditorUtils::FindLocalVariableGuidByName(WorkingBlueprint, ScopeGraph, VariableFName);
							if (!LocalGuid.IsValid())
							{
								ErrorMessage = FString::Printf(TEXT("Local variable '%s' not found in '%s'"), *VariableName, *ScopeFunctionName);
							}
							else
							{
								VariableNode->VariableReference.SetLocalMember(VariableFName, ScopeGraph->GetName(), LocalGuid);
							}
						}
					}
					else
					{
						FString PropertyError;
						if (FProperty* Property = BlueprintToolUtils::FindBlueprintProperty(WorkingBlueprint, VariableFName, PropertyError))
						{
							VariableNode->SetFromProperty(Property, true, WorkingBlueprint->SkeletonGeneratedClass ? WorkingBlueprint->SkeletonGeneratedClass : WorkingBlueprint->GeneratedClass);
						}
						else
						{
							ErrorMessage = PropertyError;
						}
					}

					TSharedPtr<FJsonObject> PinDefaults;
					TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);
					if (ErrorMessage.IsEmpty() && FinalizeSpecializedNode(WorkingBlueprint, NewNode, PinDefaults, ErrorMessage))
					{
						RecordCreatedNode(NewNode);
						ResultObject->SetStringField(TEXT("graph_name"), GraphName);
						bSuccess = true;
						bMutatedBlueprint = true;
					}
					else if (NewNode)
					{
						TargetGraph->RemoveNode(NewNode);
					}
				}
			}
			else
			{
				ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			}
		}
		else if (Op == TEXT("add_cast"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString ClassPath = GetStringArgOrDefault(Operation, TEXT("class_path"));
			FVector2D Position;
			if (ClassPath.IsEmpty())
			{
				ErrorMessage = TEXT("'class_path' is required for add_cast");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				FString ClassError;
				UClass* TargetClass = BlueprintToolUtils::ResolveClassReference(ClassPath, UObject::StaticClass(), ClassError);
				if (!TargetClass)
				{
					ErrorMessage = ClassError;
				}
				else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
				{
					UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(CreateRawGraphNode(
						WorkingBlueprint,
						TargetGraph,
						UK2Node_DynamicCast::StaticClass(),
						Position,
						ErrorMessage));
					if (CastNode)
					{
						CastNode->TargetType = TargetClass;
						TSharedPtr<FJsonObject> PinDefaults;
						TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);
						if (FinalizeSpecializedNode(WorkingBlueprint, CastNode, PinDefaults, ErrorMessage))
						{
							RecordCreatedNode(CastNode);
							ResultObject->SetStringField(TEXT("graph_name"), GraphName);
							bSuccess = true;
							bMutatedBlueprint = true;
						}
						else
						{
							TargetGraph->RemoveNode(CastNode);
						}
					}
				}
				else
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
			}
		}
		else if (Op == TEXT("add_spawn_actor") || Op == TEXT("add_create_widget"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString ClassPath = GetStringArgOrDefault(Operation, TEXT("class_path"));
			FVector2D Position;
			if (ClassPath.IsEmpty())
			{
				ErrorMessage = TEXT("'class_path' is required");
			}
			else if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				FString ClassError;
				UClass* TargetClass = BlueprintToolUtils::ResolveClassReference(ClassPath, UObject::StaticClass(), ClassError);
				if (!TargetClass)
				{
					ErrorMessage = ClassError;
				}
				else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
				{
					const FString NodeClassName = (Op == TEXT("add_spawn_actor")) ? TEXT("K2Node_SpawnActorFromClass") : TEXT("K2Node_CreateWidget");
					UClass* NodeClass = BlueprintToolUtils::ResolveClassReference(NodeClassName, UK2Node_ConstructObjectFromClass::StaticClass(), ErrorMessage);
					if (NodeClass)
					{
						UK2Node_ConstructObjectFromClass* ConstructNode = Cast<UK2Node_ConstructObjectFromClass>(CreateRawGraphNode(
							WorkingBlueprint,
							TargetGraph,
							NodeClass,
							Position,
							ErrorMessage));
						if (ConstructNode && FinalizeSpecializedNode(WorkingBlueprint, ConstructNode, nullptr, ErrorMessage))
						{
							if (UEdGraphPin* ClassPin = ConstructNode->GetClassPin())
							{
								if (BlueprintToolUtils::SetPinDefaultLiteral(ClassPin, TargetClass->GetPathName(), ErrorMessage))
								{
									ConstructNode->PinDefaultValueChanged(ClassPin);
									TSharedPtr<FJsonObject> PinDefaults;
									TryGetObjectField(Operation, TEXT("pin_defaults"), PinDefaults);
									if (PinDefaults.IsValid())
									{
										UAddGraphNodeTool::ApplyPinDefaults(ConstructNode, PinDefaults);
									}
									RecordCreatedNode(ConstructNode);
									ResultObject->SetStringField(TEXT("graph_name"), GraphName);
									bSuccess = true;
									bMutatedBlueprint = true;
								}
								else
								{
									TargetGraph->RemoveNode(ConstructNode);
								}
							}
							else
							{
								ErrorMessage = TEXT("Class pin not found on construct node");
								TargetGraph->RemoveNode(ConstructNode);
							}
						}
						else if (ConstructNode)
						{
							TargetGraph->RemoveNode(ConstructNode);
						}
					}
				}
				else
				{
					ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
				}
			}
		}
		else if (Op == TEXT("remove_node"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			UEdGraph* FoundGraph = nullptr;
			UEdGraphNode* Node = ResolveNode(WorkingBlueprint, AliasMap, NodeToken, &FoundGraph, ErrorMessage);
			if (Node && FoundGraph)
			{
				FoundGraph->RemoveNode(Node);
				MarkBlueprintGraphDirty(WorkingBlueprint);
				bSuccess = true;
				bMutatedBlueprint = true;
			}
		}
		else if (Op == TEXT("move_node"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			FVector2D Position;
			if (!TryParsePositionField(Operation, Position, ErrorMessage))
			{
				// Error populated.
			}
			else if (UEdGraphNode* Node = ResolveNode(WorkingBlueprint, AliasMap, NodeToken, nullptr, ErrorMessage))
			{
				Node->Modify();
				Node->NodePosX = static_cast<int32>(Position.X);
				Node->NodePosY = static_cast<int32>(Position.Y);
				MarkBlueprintGraphDirty(WorkingBlueprint);
				bSuccess = true;
				bMutatedBlueprint = true;
			}
		}
		else if (Op == TEXT("move_nodes_batch"))
		{
			TArray<UEdGraphNode*> Nodes;
			FVector2D Delta;
			if (!CollectOperationNodes(WorkingBlueprint, Operation, AliasMap, Nodes, ErrorMessage))
			{
				// Error populated.
			}
			else if (!TryParseDeltaField(Operation, Delta, ErrorMessage))
			{
				// Error populated.
			}
			else
			{
				for (UEdGraphNode* Node : Nodes)
				{
					Node->Modify();
					Node->NodePosX += static_cast<int32>(Delta.X);
					Node->NodePosY += static_cast<int32>(Delta.Y);
				}

				MarkBlueprintGraphDirty(WorkingBlueprint);
				ResultObject->SetNumberField(TEXT("moved_nodes"), Nodes.Num());
				bSuccess = true;
				bMutatedBlueprint = true;
			}
		}
		else if (Op == TEXT("set_node_property"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			const FString PropertyPath = GetStringArgOrDefault(Operation, TEXT("property_path"));
			const TSharedPtr<FJsonValue>* ValuePtr = Operation->Values.Find(TEXT("value"));
			if (PropertyPath.IsEmpty())
			{
				ErrorMessage = TEXT("'property_path' is required");
			}
			else if (!ValuePtr || !(*ValuePtr).IsValid())
			{
				ErrorMessage = TEXT("'value' is required");
			}
			else if (UEdGraphNode* Node = ResolveNode(WorkingBlueprint, AliasMap, NodeToken, nullptr, ErrorMessage))
			{
				FProperty* Property = nullptr;
				void* Container = nullptr;
				if (!FMcpAssetModifier::FindPropertyByPath(Node, PropertyPath, Property, Container, ErrorMessage))
				{
					ErrorMessage = FString::Printf(TEXT("Failed to find property '%s': %s"), *PropertyPath, *ErrorMessage);
				}
				else if (!FMcpAssetModifier::SetPropertyFromJson(Property, Container, *ValuePtr, ErrorMessage))
				{
					ErrorMessage = FString::Printf(TEXT("Failed to set property '%s': %s"), *PropertyPath, *ErrorMessage);
				}
				else
				{
					MarkBlueprintGraphDirty(WorkingBlueprint);
					bSuccess = true;
					bMutatedBlueprint = true;
				}
			}
		}
		else if (Op == TEXT("set_pin_default"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			const FString PinName = GetStringArgOrDefault(Operation, TEXT("pin_name"));
			const TSharedPtr<FJsonValue>* ValuePtr = Operation->Values.Find(TEXT("value"));
			if (PinName.IsEmpty())
			{
				ErrorMessage = TEXT("'pin_name' is required");
			}
			else if (!ValuePtr || !(*ValuePtr).IsValid())
			{
				ErrorMessage = TEXT("'value' is required");
			}
			else if (UEdGraphNode* Node = ResolveNode(WorkingBlueprint, AliasMap, NodeToken, nullptr, ErrorMessage))
			{
				UEdGraphPin* Pin = BlueprintToolUtils::FindPinByName(Node, PinName, EGPD_Input);
				if (!Pin)
				{
					ErrorMessage = FString::Printf(TEXT("Pin '%s' not found"), *PinName);
				}
				else
				{
					const FString LiteralValue = BlueprintToolUtils::JsonValueToDefaultString(*ValuePtr, Pin->PinType);
					if (BlueprintToolUtils::SetPinDefaultLiteral(Pin, LiteralValue, ErrorMessage))
					{
						MarkBlueprintGraphDirty(WorkingBlueprint);
						bSuccess = true;
						bMutatedBlueprint = true;
					}
				}
			}
		}
		else if (Op == TEXT("connect") || Op == TEXT("disconnect"))
		{
			const FString SourceToken = GetStringArgOrDefault(Operation, TEXT("source_node"));
			const FString SourcePinName = GetStringArgOrDefault(Operation, TEXT("source_pin"), GetStringArgOrDefault(Operation, TEXT("pin_name")));
			const FString TargetToken = GetStringArgOrDefault(Operation, TEXT("target_node"));
			const FString TargetPinName = GetStringArgOrDefault(Operation, TEXT("target_pin"));

			if (SourcePinName.IsEmpty())
			{
				ErrorMessage = TEXT("'source_pin' (or 'pin_name') is required");
			}
			else
			{
				UEdGraphNode* SourceNode = ResolveNode(WorkingBlueprint, AliasMap, SourceToken, nullptr, ErrorMessage);
				UEdGraphNode* TargetNode = nullptr;
				if (SourceNode && Op == TEXT("connect"))
				{
					if (TargetPinName.IsEmpty())
					{
						ErrorMessage = TEXT("'target_node' and 'target_pin' are required");
					}
					else
					{
						TargetNode = ResolveNode(WorkingBlueprint, AliasMap, TargetToken, nullptr, ErrorMessage);
					}
				}
				else if (SourceNode && !TargetToken.IsEmpty())
				{
					TargetNode = ResolveNode(WorkingBlueprint, AliasMap, TargetToken, nullptr, ErrorMessage);
				}

				if (SourceNode && ErrorMessage.IsEmpty())
				{
					UEdGraphPin* SourcePin = BlueprintToolUtils::FindPinByName(SourceNode, SourcePinName);
					if (!SourcePin)
					{
						ErrorMessage = TEXT("Source pin not found");
					}
					else if (Op == TEXT("connect"))
					{
						UEdGraphPin* TargetPin = BlueprintToolUtils::FindPinByName(TargetNode, TargetPinName);
						const UEdGraphSchema* Schema = SourceNode->GetGraph() ? SourceNode->GetGraph()->GetSchema() : nullptr;
						if (!TargetPin)
						{
							ErrorMessage = TEXT("Target pin not found");
						}
						else if (!Schema)
						{
							ErrorMessage = TEXT("Graph schema not available");
						}
						else
						{
							FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
							if (Response.Response == CONNECT_RESPONSE_DISALLOW)
							{
								ErrorMessage = Response.Message.ToString();
							}
							else if (!Schema->TryCreateConnection(SourcePin, TargetPin))
							{
								ErrorMessage = TEXT("Failed to create pin connection");
							}
							else
							{
								MarkBlueprintGraphDirty(WorkingBlueprint);
								bSuccess = true;
								bMutatedBlueprint = true;
							}
						}
					}
					else if (TargetNode && !TargetPinName.IsEmpty())
					{
						UEdGraphPin* TargetPin = BlueprintToolUtils::FindPinByName(TargetNode, TargetPinName);
						if (!TargetPin)
						{
							ErrorMessage = TEXT("Target pin not found");
						}
						else
						{
							SourcePin->BreakLinkTo(TargetPin);
							MarkBlueprintGraphDirty(WorkingBlueprint);
							bSuccess = true;
							bMutatedBlueprint = true;
						}
					}
					else
					{
						SourcePin->BreakAllPinLinks();
						MarkBlueprintGraphDirty(WorkingBlueprint);
						bSuccess = true;
						bMutatedBlueprint = true;
					}
				}
			}
		}
		else if (Op == TEXT("auto_connect"))
		{
			const FString SourceToken = GetStringArgOrDefault(Operation, TEXT("source_node"));
			const FString TargetToken = GetStringArgOrDefault(Operation, TEXT("target_node"));
			UEdGraphNode* SourceNode = ResolveNode(WorkingBlueprint, AliasMap, SourceToken, nullptr, ErrorMessage);
			UEdGraphNode* TargetNode = SourceNode ? ResolveNode(WorkingBlueprint, AliasMap, TargetToken, nullptr, ErrorMessage) : nullptr;
			if (SourceNode && TargetNode)
			{
				TArray<TSharedPtr<FJsonValue>> ConnectionResults;
				if (BlueprintToolUtils::AutoConnectNodes(SourceNode, TargetNode, &ConnectionResults, ErrorMessage))
				{
					ResultObject->SetArrayField(TEXT("connections"), ConnectionResults);
					MarkBlueprintGraphDirty(WorkingBlueprint);
					bSuccess = true;
					bMutatedBlueprint = true;
				}
			}
		}
		else if (Op == TEXT("comment_region"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			TArray<UEdGraphNode*> Nodes;
			if (!CollectOperationNodes(WorkingBlueprint, Operation, AliasMap, Nodes, ErrorMessage))
			{
				// Error populated.
			}
			else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				const FBox2D Bounds = BlueprintToolUtils::ComputeNodeBounds(Nodes, 80.0f);
				FString CreateError;
				UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(CreateRawGraphNode(
					WorkingBlueprint,
					TargetGraph,
					UEdGraphNode_Comment::StaticClass(),
					Bounds.Min,
					CreateError));
				if (!CommentNode)
				{
					ErrorMessage = CreateError;
				}
				else
				{
					CommentNode->NodeComment = GetStringArgOrDefault(Operation, TEXT("comment_text"), TEXT("Comment"));
					CommentNode->MoveMode = GetBoolArgOrDefault(Operation, TEXT("group_movement"), true)
						? ECommentBoxMode::GroupMovement
						: ECommentBoxMode::NoGroupMovement;

					FLinearColor CommentColor;
					if (TryParseColorField(Operation, TEXT("comment_color"), CommentColor))
					{
						CommentNode->CommentColor = CommentColor;
					}

					CommentNode->SetBounds(FSlateRect(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y));
					for (UEdGraphNode* Node : Nodes)
					{
						CommentNode->AddNodeUnderComment(Node);
					}

					MarkBlueprintGraphDirty(WorkingBlueprint);
					RecordCreatedNode(CommentNode);
					ResultObject->SetNumberField(TEXT("covered_nodes"), Nodes.Num());
					ResultObject->SetStringField(TEXT("graph_name"), GraphName);
					bSuccess = true;
					bMutatedBlueprint = true;
				}
			}
			else
			{
				ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			}
		}
		else if (Op == TEXT("layout_graph"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			TArray<UEdGraphNode*> Nodes;
			if (Operation->HasField(TEXT("nodes")))
			{
				if (!CollectOperationNodes(WorkingBlueprint, Operation, AliasMap, Nodes, ErrorMessage))
				{
					// Error populated.
				}
			}
			else if (UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(WorkingBlueprint, GraphName))
			{
				for (UEdGraphNode* Node : TargetGraph->Nodes)
				{
					if (Node && !Node->IsA<UEdGraphNode_Comment>())
					{
						Nodes.Add(Node);
					}
				}
			}
			else
			{
				ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			}

			if (ErrorMessage.IsEmpty())
			{
				const int32 StartX = GetIntArgOrDefault(Operation, TEXT("start_x"), 0);
				const int32 StartY = GetIntArgOrDefault(Operation, TEXT("start_y"), 0);
				const int32 SpacingX = GetIntArgOrDefault(Operation, TEXT("spacing_x"), 380);
				const int32 SpacingY = GetIntArgOrDefault(Operation, TEXT("spacing_y"), 220);
				BlueprintToolUtils::LayoutNodesSimple(Nodes, StartX, StartY, SpacingX, SpacingY);
				MarkBlueprintGraphDirty(WorkingBlueprint);
				ResultObject->SetNumberField(TEXT("layout_nodes"), Nodes.Num());
				ResultObject->SetStringField(TEXT("graph_name"), GraphName);
				bSuccess = true;
				bMutatedBlueprint = true;
			}
		}
		else
		{
			ErrorMessage = FString::Printf(TEXT("Unsupported op: %s"), *Op);
		}

		ResultObject->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), ErrorMessage);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
		}
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));

		if (bAnyFailed && bRollbackOnError)
		{
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();
			}

			TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
			PartialResult->SetStringField(TEXT("asset_path"), AssetPath);
			PartialResult->SetArrayField(TEXT("results"), ResultsArray);
			PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_GRAPH_EDIT_FAILED"), ErrorMessage, nullptr, PartialResult);
		}
	}

	if (!bDryRun && !bAnyFailed && bMutatedBlueprint)
	{
		if (bCompile)
		{
			FString CompileError;
			if (!FMcpAssetModifier::CompileBlueprint(Blueprint, CompileError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
			}
		}

		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Result->SetObjectField(TEXT("summary"), MakeSummaryObject(OperationsArray->Num(), PartialResultsArray.Num(), !bDryRun && bCompile && !bAnyFailed && bMutatedBlueprint, !bDryRun && bSave && !bAnyFailed && bMutatedBlueprint));
	return FMcpToolResult::StructuredSuccess(Result, bDryRun ? TEXT("Blueprint graph dry-run complete") : TEXT("Blueprint graph edit complete"));
}
