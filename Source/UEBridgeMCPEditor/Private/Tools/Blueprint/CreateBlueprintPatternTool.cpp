#include "Tools/Blueprint/CreateBlueprintPatternTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Blueprint/BlueprintToolUtils.h"
#include "Tools/Blueprint/CreateBlueprintEventTool.h"
#include "Tools/Blueprint/EditBlueprintGraphTool.h"
#include "Tools/Blueprint/EditBlueprintMembersTool.h"
#include "Tools/Blueprint/LayoutBlueprintGraphTool.h"
#include "Tools/Write/CreateAssetTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"

namespace
{
	struct FFunctionGraphAnchors
	{
		FString EntryNodeGuid;
		FString ResultNodeGuid;
	};

	TSharedPtr<FJsonObject> MakeTypeDescriptor(const FString& Kind, const FString& ObjectClassPath = FString())
	{
		TSharedPtr<FJsonObject> TypeObject = MakeShareable(new FJsonObject);
		TypeObject->SetStringField(TEXT("kind"), Kind);
		if (!ObjectClassPath.IsEmpty())
		{
			TypeObject->SetStringField(TEXT("object_class"), ObjectClassPath);
		}
		return TypeObject;
	}

	TSharedPtr<FJsonObject> MakePinDescriptor(const FString& Name, const TSharedPtr<FJsonObject>& TypeObject)
	{
		TSharedPtr<FJsonObject> PinObject = MakeShareable(new FJsonObject);
		PinObject->SetStringField(TEXT("name"), Name);
		PinObject->SetObjectField(TEXT("type"), TypeObject);
		return PinObject;
	}

	TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
	{
		return MakeShareable(new FJsonValueString(Value));
	}

	TSharedPtr<FJsonValue> MakeNumberValue(const double Value)
	{
		return MakeShareable(new FJsonValueNumber(Value));
	}

	TArray<TSharedPtr<FJsonValue>> MakePosition(const double X, const double Y)
	{
		return {
			MakeNumberValue(X),
			MakeNumberValue(Y)
		};
	}

	TArray<TSharedPtr<FJsonValue>> MakeNodeRefs(const TArray<FString>& Nodes)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Node : Nodes)
		{
			Result.Add(MakeStringValue(Node));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> MakeOperation(const FString& Op, const FString& GraphName)
	{
		TSharedPtr<FJsonObject> Operation = MakeShareable(new FJsonObject);
		Operation->SetStringField(TEXT("op"), Op);
		if (!GraphName.IsEmpty())
		{
			Operation->SetStringField(TEXT("graph_name"), GraphName);
		}
		return Operation;
	}

	FString ReadToolMessage(const FMcpToolResult& Result, const FString& DefaultMessage)
	{
		if (const TSharedPtr<FJsonObject> Structured = Result.GetStructuredContent(); Structured.IsValid())
		{
			FString Message;
			if (Structured->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
			{
				return Message;
			}
			if (Structured->TryGetStringField(TEXT("error"), Message) && !Message.IsEmpty())
			{
				return Message;
			}
		}

		return DefaultMessage;
	}

	void AddStep(
		const FString& StepName,
		const bool bSuccess,
		const TSharedPtr<FJsonObject>& Payload,
		TArray<TSharedPtr<FJsonValue>>& OutSteps)
	{
		TSharedPtr<FJsonObject> StepObject = MakeShareable(new FJsonObject);
		StepObject->SetStringField(TEXT("step"), StepName);
		StepObject->SetBoolField(TEXT("success"), bSuccess);
		if (Payload.IsValid())
		{
			StepObject->SetObjectField(TEXT("payload"), Payload);
		}
		OutSteps.Add(MakeShareable(new FJsonValueObject(StepObject)));
	}

	bool InvokeStructuredTool(
		UMcpToolBase* Tool,
		const FString& StepName,
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		TSharedPtr<FJsonObject>& OutPayload,
		FString& OutError)
	{
		const FMcpToolResult Result = Tool->Execute(Arguments, Context);
		OutPayload = Result.GetStructuredContent();
		AddStep(StepName, Result.bSuccess, OutPayload, OutSteps);
		if (!Result.bSuccess)
		{
			TSharedPtr<FJsonObject> Partial = MakeShareable(new FJsonObject);
			Partial->SetStringField(TEXT("step"), StepName);
			Partial->SetBoolField(TEXT("success"), false);
			if (OutPayload.IsValid())
			{
				Partial->SetObjectField(TEXT("payload"), OutPayload);
			}
			OutPartialResults.Add(MakeShareable(new FJsonValueObject(Partial)));
			OutError = ReadToolMessage(Result, FString::Printf(TEXT("%s failed"), *StepName));
			return false;
		}

		return true;
	}

	bool ResolveFunctionGraphAnchors(
		UBlueprint* Blueprint,
		const FString& GraphName,
		FFunctionGraphAnchors& OutAnchors,
		FString& OutError)
	{
		UEdGraph* Graph = FMcpAssetModifier::FindGraphByName(Blueprint, GraphName);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Graph '%s' not found"), *GraphName);
			return false;
		}

		UK2Node_FunctionEntry* EntryNode = nullptr;
		UK2Node_FunctionResult* ResultNode = nullptr;
		if (!BlueprintToolUtils::FindFunctionNodes(Graph, EntryNode, ResultNode, OutError))
		{
			return false;
		}

		OutAnchors.EntryNodeGuid = EntryNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		if (ResultNode)
		{
			OutAnchors.ResultNodeGuid = ResultNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		}

		return true;
	}

	bool ReadCreatedNodeGuid(const TSharedPtr<FJsonObject>& Payload, FString& OutNodeGuid)
	{
		if (!Payload.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ResultsArray = nullptr;
		if (!Payload->TryGetArrayField(TEXT("results"), ResultsArray) || !ResultsArray || ResultsArray->Num() == 0)
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* ResultObject = nullptr;
		if (!(*ResultsArray)[0]->TryGetObject(ResultObject) || !ResultObject || !(*ResultObject).IsValid())
		{
			return false;
		}

		return (*ResultObject)->TryGetStringField(TEXT("node_guid"), OutNodeGuid);
	}

	bool ReadCreatedNodeGuidByAlias(const TSharedPtr<FJsonObject>& Payload, const FString& Alias, FString& OutNodeGuid)
	{
		if (!Payload.IsValid() || Alias.IsEmpty())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ResultsArray = nullptr;
		if (!Payload->TryGetArrayField(TEXT("results"), ResultsArray) || !ResultsArray)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& ResultValue : *ResultsArray)
		{
			const TSharedPtr<FJsonObject>* ResultObject = nullptr;
			if (!ResultValue.IsValid() || !ResultValue->TryGetObject(ResultObject) || !ResultObject || !(*ResultObject).IsValid())
			{
				continue;
			}

			FString ResultAlias;
			if ((*ResultObject)->TryGetStringField(TEXT("alias"), ResultAlias)
				&& ResultAlias == Alias)
			{
				return (*ResultObject)->TryGetStringField(TEXT("node_guid"), OutNodeGuid);
			}
		}

		return false;
	}

	bool RequireCreatedNodeGuidByAlias(const TSharedPtr<FJsonObject>& Payload, const FString& Alias, FString& OutNodeGuid, FString& OutError)
	{
		if (ReadCreatedNodeGuidByAlias(Payload, Alias, OutNodeGuid))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Failed to read node GUID for alias '%s'"), *Alias);
		return false;
	}

	bool ExecuteGraphBatch(
		const FString& AssetPath,
		const TArray<TSharedPtr<FJsonValue>>& Operations,
		const FString& StepName,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		FString& OutError,
		TSharedPtr<FJsonObject>* OutPayload = nullptr)
	{
		TSharedPtr<FJsonObject> Arguments = MakeShareable(new FJsonObject);
		Arguments->SetStringField(TEXT("asset_path"), AssetPath);
		Arguments->SetBoolField(TEXT("compile"), false);
		Arguments->SetBoolField(TEXT("save"), false);
		Arguments->SetBoolField(TEXT("rollback_on_error"), true);
		Arguments->SetArrayField(TEXT("operations"), Operations);

		UEditBlueprintGraphTool* Tool = NewObject<UEditBlueprintGraphTool>();
		TSharedPtr<FJsonObject> Payload;
		const bool bSuccess = InvokeStructuredTool(Tool, StepName, Arguments, Context, OutSteps, OutPartialResults, Payload, OutError);
		if (OutPayload)
		{
			*OutPayload = Payload;
		}
		return bSuccess;
	}

	bool ExecuteLayout(
		const FString& AssetPath,
		const FString& GraphName,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		FString& OutError)
	{
		TSharedPtr<FJsonObject> Arguments = MakeShareable(new FJsonObject);
		Arguments->SetStringField(TEXT("asset_path"), AssetPath);
		Arguments->SetStringField(TEXT("graph_name"), GraphName);
		Arguments->SetStringField(TEXT("compile"), TEXT("never"));
		Arguments->SetBoolField(TEXT("save"), false);
		Arguments->SetBoolField(TEXT("rollback_on_error"), true);

		ULayoutBlueprintGraphTool* Tool = NewObject<ULayoutBlueprintGraphTool>();
		TSharedPtr<FJsonObject> Payload;
		return InvokeStructuredTool(Tool, FString::Printf(TEXT("layout_%s"), *GraphName), Arguments, Context, OutSteps, OutPartialResults, Payload, OutError);
	}

	bool ExecuteCommentRegion(
		const FString& AssetPath,
		const FString& GraphName,
		const FString& CommentText,
		const TArray<FString>& Nodes,
		const FString& StepName,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		FString& OutError)
	{
		TSharedPtr<FJsonObject> Operation = MakeOperation(TEXT("comment_region"), GraphName);
		Operation->SetStringField(TEXT("comment_text"), CommentText);
		Operation->SetArrayField(TEXT("nodes"), MakeNodeRefs(Nodes));
		TArray<TSharedPtr<FJsonValue>> Operations = { MakeShareable(new FJsonValueObject(Operation)) };
		return ExecuteGraphBatch(AssetPath, Operations, StepName, Context, OutSteps, OutPartialResults, OutError);
	}

	void AddConnectOperation(TArray<TSharedPtr<FJsonValue>>& Operations, const FString& SourceNode, const FString& TargetNode)
	{
		TSharedPtr<FJsonObject> Operation = MakeOperation(TEXT("auto_connect"), FString());
		Operation->SetStringField(TEXT("source_node"), SourceNode);
		Operation->SetStringField(TEXT("target_node"), TargetNode);
		Operations.Add(MakeShareable(new FJsonValueObject(Operation)));
	}

	void AddPinConnectOperation(
		TArray<TSharedPtr<FJsonValue>>& Operations,
		const FString& SourceNode,
		const FString& SourcePin,
		const FString& TargetNode,
		const FString& TargetPin)
	{
		TSharedPtr<FJsonObject> Operation = MakeOperation(TEXT("connect"), FString());
		Operation->SetStringField(TEXT("source_node"), SourceNode);
		Operation->SetStringField(TEXT("source_pin"), SourcePin);
		Operation->SetStringField(TEXT("target_node"), TargetNode);
		Operation->SetStringField(TEXT("target_pin"), TargetPin);
		Operations.Add(MakeShareable(new FJsonValueObject(Operation)));
	}

	TSharedPtr<FJsonObject> MakeFunctionAction(
		const FString& Name,
		const TArray<TSharedPtr<FJsonValue>>& Inputs,
		const TArray<TSharedPtr<FJsonValue>>& Outputs,
		const bool bPure = false,
		const bool bConst = false)
	{
		TSharedPtr<FJsonObject> Action = MakeShareable(new FJsonObject);
		Action->SetStringField(TEXT("action"), TEXT("create_function"));
		Action->SetStringField(TEXT("name"), Name);
		if (Inputs.Num() > 0)
		{
			Action->SetArrayField(TEXT("inputs"), Inputs);
		}
		if (Outputs.Num() > 0)
		{
			Action->SetArrayField(TEXT("outputs"), Outputs);
		}
		if (bPure)
		{
			Action->SetBoolField(TEXT("pure"), true);
		}
		if (bConst)
		{
			Action->SetBoolField(TEXT("const"), true);
		}
		return Action;
	}
}

FString UCreateBlueprintPatternTool::GetToolDescription() const
{
	return TEXT("Create a curated engine-only Actor Blueprint pattern by orchestrating asset creation, member declarations, graph setup, layout, and final compile.");
}

TMap<FString, FMcpSchemaProperty> UCreateBlueprintPatternTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("New Blueprint asset path"), true));
	Schema.Add(TEXT("pattern"), FMcpSchemaProperty::MakeEnum(
		TEXT("Built-in Actor Blueprint pattern"),
		{
			TEXT("logic_actor_skeleton"),
			TEXT("toggle_state_actor"),
			TEXT("interaction_stub_actor")
		},
		true));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Final compile policy: 'never' or 'final'"), {TEXT("never"), TEXT("final")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after successful generation")));
	return Schema;
}

TArray<FString> UCreateBlueprintPatternTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("pattern")};
}

FMcpToolResult UCreateBlueprintPatternTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString Pattern = GetStringArgOrDefault(Arguments, TEXT("pattern"));
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("final"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);

	if (!CompilePolicy.Equals(TEXT("never"), ESearchCase::IgnoreCase)
		&& !CompilePolicy.Equals(TEXT("final"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("'compile' must be 'never' or 'final'"));
	}

	if (Pattern != TEXT("logic_actor_skeleton")
		&& Pattern != TEXT("toggle_state_actor")
		&& Pattern != TEXT("interaction_stub_actor"))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), FString::Printf(TEXT("Unsupported pattern '%s'"), *Pattern));
	}

	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Asset already exists"), Details);
	}

	TArray<TSharedPtr<FJsonValue>> Steps;
	TArray<TSharedPtr<FJsonValue>> PartialResults;
	FString StepError;

	{
		TSharedPtr<FJsonObject> CreateArgs = MakeShareable(new FJsonObject);
		CreateArgs->SetStringField(TEXT("asset_path"), AssetPath);
		CreateArgs->SetStringField(TEXT("asset_class"), TEXT("Blueprint"));
		CreateArgs->SetStringField(TEXT("parent_class"), TEXT("/Script/Engine.Actor"));

		UCreateAssetTool* CreateAssetTool = NewObject<UCreateAssetTool>();
		const FMcpToolResult CreateResult = CreateAssetTool->Execute(CreateArgs, Context);
		TSharedPtr<FJsonObject> StepPayload = MakeShareable(new FJsonObject);
		StepPayload->SetStringField(TEXT("asset_path"), AssetPath);
		StepPayload->SetStringField(TEXT("pattern"), Pattern);
		AddStep(TEXT("create_asset"), CreateResult.bSuccess, StepPayload, Steps);
		if (!CreateResult.bSuccess)
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("asset_path"), AssetPath);
			Details->SetStringField(TEXT("pattern"), Pattern);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_CREATE_FAILED"), TEXT("create-asset failed while creating the pattern Blueprint"), Details);
		}
	}

	TArray<TSharedPtr<FJsonValue>> MemberActions;
	if (Pattern == TEXT("logic_actor_skeleton"))
	{
		TSharedPtr<FJsonObject> VariableAction = MakeShareable(new FJsonObject);
		VariableAction->SetStringField(TEXT("action"), TEXT("create_variable"));
		VariableAction->SetStringField(TEXT("name"), TEXT("bEnabled"));
		VariableAction->SetObjectField(TEXT("type"), MakeTypeDescriptor(TEXT("bool")));
		VariableAction->SetBoolField(TEXT("default_value"), true);
		VariableAction->SetStringField(TEXT("category"), TEXT("Logic"));
		MemberActions.Add(MakeShareable(new FJsonValueObject(VariableAction)));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(TEXT("InitializeLogic"), {}, {}))));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(TEXT("ExecuteLogic"), {}, {}))));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(TEXT("ResetLogic"), {}, {}))));
	}
	else if (Pattern == TEXT("toggle_state_actor"))
	{
		TSharedPtr<FJsonObject> VariableAction = MakeShareable(new FJsonObject);
		VariableAction->SetStringField(TEXT("action"), TEXT("create_variable"));
		VariableAction->SetStringField(TEXT("name"), TEXT("bIsActive"));
		VariableAction->SetObjectField(TEXT("type"), MakeTypeDescriptor(TEXT("bool")));
		VariableAction->SetBoolField(TEXT("default_value"), false);
		VariableAction->SetStringField(TEXT("category"), TEXT("State"));
		MemberActions.Add(MakeShareable(new FJsonValueObject(VariableAction)));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(
			TEXT("SetIsActive"),
			{ MakeShareable(new FJsonValueObject(MakePinDescriptor(TEXT("NewValue"), MakeTypeDescriptor(TEXT("bool"))))) },
			{}))));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(TEXT("ToggleIsActive"), {}, {}))));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(
			TEXT("GetIsActive"),
			{},
			{ MakeShareable(new FJsonValueObject(MakePinDescriptor(TEXT("ReturnValue"), MakeTypeDescriptor(TEXT("bool"))))) },
			true,
			true))));
	}
	else if (Pattern == TEXT("interaction_stub_actor"))
	{
		TSharedPtr<FJsonObject> VariableAction = MakeShareable(new FJsonObject);
		VariableAction->SetStringField(TEXT("action"), TEXT("create_variable"));
		VariableAction->SetStringField(TEXT("name"), TEXT("bCanInteract"));
		VariableAction->SetObjectField(TEXT("type"), MakeTypeDescriptor(TEXT("bool")));
		VariableAction->SetBoolField(TEXT("default_value"), true);
		VariableAction->SetStringField(TEXT("category"), TEXT("Interaction"));
		MemberActions.Add(MakeShareable(new FJsonValueObject(VariableAction)));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(
			TEXT("CanInteract"),
			{ MakeShareable(new FJsonValueObject(MakePinDescriptor(TEXT("Interactor"), MakeTypeDescriptor(TEXT("object"), TEXT("/Script/Engine.Actor"))))) },
			{ MakeShareable(new FJsonValueObject(MakePinDescriptor(TEXT("ReturnValue"), MakeTypeDescriptor(TEXT("bool"))))) },
			true,
			true))));
		MemberActions.Add(MakeShareable(new FJsonValueObject(MakeFunctionAction(
			TEXT("Interact"),
			{ MakeShareable(new FJsonValueObject(MakePinDescriptor(TEXT("Interactor"), MakeTypeDescriptor(TEXT("object"), TEXT("/Script/Engine.Actor"))))) },
			{}))));
	}
	else
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), FString::Printf(TEXT("Unsupported pattern '%s'"), *Pattern));
	}

	{
		TSharedPtr<FJsonObject> MemberArgs = MakeShareable(new FJsonObject);
		MemberArgs->SetStringField(TEXT("asset_path"), AssetPath);
		MemberArgs->SetStringField(TEXT("compile"), TEXT("never"));
		MemberArgs->SetBoolField(TEXT("save"), false);
		MemberArgs->SetBoolField(TEXT("rollback_on_error"), true);
		MemberArgs->SetArrayField(TEXT("actions"), MemberActions);

		UEditBlueprintMembersTool* MembersTool = NewObject<UEditBlueprintMembersTool>();
		TSharedPtr<FJsonObject> Payload;
		if (!InvokeStructuredTool(MembersTool, TEXT("edit_members"), MemberArgs, Context, Steps, PartialResults, Payload, StepError))
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("asset_path"), AssetPath);
			Details->SetStringField(TEXT("pattern"), Pattern);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_MEMBER_SETUP_FAILED"), StepError, Details);
		}
	}

	FString EventNodeGuid;
	if (Pattern == TEXT("logic_actor_skeleton") || Pattern == TEXT("toggle_state_actor"))
	{
		TSharedPtr<FJsonObject> EventArgs = MakeShareable(new FJsonObject);
		EventArgs->SetStringField(TEXT("asset_path"), AssetPath);
		EventArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
		EventArgs->SetStringField(TEXT("name"), Pattern == TEXT("logic_actor_skeleton") ? TEXT("RunLogic") : TEXT("RequestToggle"));
		EventArgs->SetStringField(TEXT("compile"), TEXT("never"));
		EventArgs->SetBoolField(TEXT("save"), false);
		EventArgs->SetArrayField(TEXT("position"), MakePosition(0.0, 0.0));

		UCreateBlueprintEventTool* EventTool = NewObject<UCreateBlueprintEventTool>();
		TSharedPtr<FJsonObject> Payload;
		if (!InvokeStructuredTool(EventTool, TEXT("create_event"), EventArgs, Context, Steps, PartialResults, Payload, StepError))
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("asset_path"), AssetPath);
			Details->SetStringField(TEXT("pattern"), Pattern);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_EVENT_SETUP_FAILED"), StepError, Details);
		}

		if (!ReadCreatedNodeGuid(Payload, EventNodeGuid))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), TEXT("Failed to read created event node GUID"));
		}
	}

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	if (Pattern == TEXT("logic_actor_skeleton"))
	{
		TArray<TSharedPtr<FJsonValue>> Operations;
		{
			TSharedPtr<FJsonObject> CallExecute = MakeOperation(TEXT("add_call_function"), TEXT("EventGraph"));
			CallExecute->SetStringField(TEXT("alias"), TEXT("call_execute_logic"));
			CallExecute->SetStringField(TEXT("function_name"), TEXT("ExecuteLogic"));
			CallExecute->SetArrayField(TEXT("position"), MakePosition(420.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(CallExecute)));
		}
		AddConnectOperation(Operations, EventNodeGuid, TEXT("call_execute_logic"));

		TSharedPtr<FJsonObject> GraphPayload;
		if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_event_graph"), Context, Steps, PartialResults, StepError, &GraphPayload))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
		}
		FString CallExecuteGuid;
		if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("call_execute_logic"), CallExecuteGuid, StepError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
		}
		if (!ExecuteLayout(AssetPath, TEXT("EventGraph"), Context, Steps, PartialResults, StepError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_LAYOUT_FAILED"), StepError);
		}
		if (!ExecuteCommentRegion(AssetPath, TEXT("EventGraph"), TEXT("Run Logic"), {EventNodeGuid, CallExecuteGuid}, TEXT("comment_event_graph"), Context, Steps, PartialResults, StepError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
		}
	}
	else if (Pattern == TEXT("toggle_state_actor"))
	{
		FFunctionGraphAnchors SetAnchors;
		FFunctionGraphAnchors ToggleAnchors;
		FFunctionGraphAnchors GetAnchors;
		if (!ResolveFunctionGraphAnchors(Blueprint, TEXT("SetIsActive"), SetAnchors, StepError)
			|| !ResolveFunctionGraphAnchors(Blueprint, TEXT("ToggleIsActive"), ToggleAnchors, StepError)
			|| !ResolveFunctionGraphAnchors(Blueprint, TEXT("GetIsActive"), GetAnchors, StepError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
		}

		{
			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedPtr<FJsonObject> SetVariable = MakeOperation(TEXT("add_set_variable"), TEXT("SetIsActive"));
			SetVariable->SetStringField(TEXT("alias"), TEXT("set_active_value"));
			SetVariable->SetStringField(TEXT("variable_name"), TEXT("bIsActive"));
			SetVariable->SetArrayField(TEXT("position"), MakePosition(420.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(SetVariable)));
			AddConnectOperation(Operations, SetAnchors.EntryNodeGuid, TEXT("set_active_value"));
			if (!SetAnchors.ResultNodeGuid.IsEmpty())
			{
				AddConnectOperation(Operations, TEXT("set_active_value"), SetAnchors.ResultNodeGuid);
			}

			TSharedPtr<FJsonObject> GraphPayload;
			if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_set_is_active"), Context, Steps, PartialResults, StepError, &GraphPayload))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
			FString SetVariableGuid;
			if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("set_active_value"), SetVariableGuid, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
			}
			if (!ExecuteLayout(AssetPath, TEXT("SetIsActive"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("SetIsActive"), TEXT("Set Active"), {SetAnchors.EntryNodeGuid, SetVariableGuid}, TEXT("comment_set_is_active"), Context, Steps, PartialResults, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
		}

		{
			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedPtr<FJsonObject> GetVariable = MakeOperation(TEXT("add_get_variable"), TEXT("GetIsActive"));
			GetVariable->SetStringField(TEXT("alias"), TEXT("get_active_value"));
			GetVariable->SetStringField(TEXT("variable_name"), TEXT("bIsActive"));
			GetVariable->SetArrayField(TEXT("position"), MakePosition(360.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(GetVariable)));
			if (!GetAnchors.ResultNodeGuid.IsEmpty())
			{
				AddConnectOperation(Operations, TEXT("get_active_value"), GetAnchors.ResultNodeGuid);
			}

			TSharedPtr<FJsonObject> GraphPayload;
			if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_get_is_active"), Context, Steps, PartialResults, StepError, &GraphPayload))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
			FString GetVariableGuid;
			if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("get_active_value"), GetVariableGuid, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
			}
			if (!ExecuteLayout(AssetPath, TEXT("GetIsActive"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("GetIsActive"), TEXT("Read Active State"), {GetVariableGuid}, TEXT("comment_get_is_active"), Context, Steps, PartialResults, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
		}

		{
			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedPtr<FJsonObject> GetVariable = MakeOperation(TEXT("add_get_variable"), TEXT("ToggleIsActive"));
			GetVariable->SetStringField(TEXT("alias"), TEXT("toggle_get_active"));
			GetVariable->SetStringField(TEXT("variable_name"), TEXT("bIsActive"));
			GetVariable->SetArrayField(TEXT("position"), MakePosition(300.0, -40.0));
			Operations.Add(MakeShareable(new FJsonValueObject(GetVariable)));

			TSharedPtr<FJsonObject> NotNode = MakeOperation(TEXT("add_call_function"), TEXT("ToggleIsActive"));
			NotNode->SetStringField(TEXT("alias"), TEXT("toggle_invert_active"));
			NotNode->SetStringField(TEXT("function_class_path"), TEXT("/Script/Engine.KismetMathLibrary"));
			NotNode->SetStringField(TEXT("function_name"), TEXT("Not_PreBool"));
			NotNode->SetArrayField(TEXT("position"), MakePosition(620.0, -40.0));
			Operations.Add(MakeShareable(new FJsonValueObject(NotNode)));

			TSharedPtr<FJsonObject> SetCall = MakeOperation(TEXT("add_call_function"), TEXT("ToggleIsActive"));
			SetCall->SetStringField(TEXT("alias"), TEXT("toggle_set_active"));
			SetCall->SetStringField(TEXT("function_name"), TEXT("SetIsActive"));
			SetCall->SetArrayField(TEXT("position"), MakePosition(920.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(SetCall)));

			AddConnectOperation(Operations, TEXT("toggle_get_active"), TEXT("toggle_invert_active"));
			AddConnectOperation(Operations, TEXT("toggle_invert_active"), TEXT("toggle_set_active"));
			AddConnectOperation(Operations, ToggleAnchors.EntryNodeGuid, TEXT("toggle_set_active"));
			if (!ToggleAnchors.ResultNodeGuid.IsEmpty())
			{
				AddConnectOperation(Operations, TEXT("toggle_set_active"), ToggleAnchors.ResultNodeGuid);
			}

			TSharedPtr<FJsonObject> GraphPayload;
			if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_toggle_is_active"), Context, Steps, PartialResults, StepError, &GraphPayload))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
			FString ToggleGetGuid;
			FString ToggleInvertGuid;
			FString ToggleSetGuid;
			if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("toggle_get_active"), ToggleGetGuid, StepError)
				|| !RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("toggle_invert_active"), ToggleInvertGuid, StepError)
				|| !RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("toggle_set_active"), ToggleSetGuid, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
			}
			if (!ExecuteLayout(AssetPath, TEXT("ToggleIsActive"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("ToggleIsActive"), TEXT("Toggle Active"), {ToggleGetGuid, ToggleInvertGuid, ToggleSetGuid}, TEXT("comment_toggle_is_active"), Context, Steps, PartialResults, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
		}

		{
			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedPtr<FJsonObject> ToggleCall = MakeOperation(TEXT("add_call_function"), TEXT("EventGraph"));
			ToggleCall->SetStringField(TEXT("alias"), TEXT("request_toggle_call"));
			ToggleCall->SetStringField(TEXT("function_name"), TEXT("ToggleIsActive"));
			ToggleCall->SetArrayField(TEXT("position"), MakePosition(420.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(ToggleCall)));
			AddConnectOperation(Operations, EventNodeGuid, TEXT("request_toggle_call"));

			TSharedPtr<FJsonObject> GraphPayload;
			if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_toggle_event_graph"), Context, Steps, PartialResults, StepError, &GraphPayload))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
			FString ToggleCallGuid;
			if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("request_toggle_call"), ToggleCallGuid, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
			}
			if (!ExecuteLayout(AssetPath, TEXT("EventGraph"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("EventGraph"), TEXT("Toggle Request"), {EventNodeGuid, ToggleCallGuid}, TEXT("comment_toggle_event_graph"), Context, Steps, PartialResults, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
		}
	}
	else if (Pattern == TEXT("interaction_stub_actor"))
	{
		FFunctionGraphAnchors CanInteractAnchors;
		FFunctionGraphAnchors InteractAnchors;
		if (!ResolveFunctionGraphAnchors(Blueprint, TEXT("CanInteract"), CanInteractAnchors, StepError)
			|| !ResolveFunctionGraphAnchors(Blueprint, TEXT("Interact"), InteractAnchors, StepError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
		}

		{
			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedPtr<FJsonObject> GetVariable = MakeOperation(TEXT("add_get_variable"), TEXT("CanInteract"));
			GetVariable->SetStringField(TEXT("alias"), TEXT("can_interact_state"));
			GetVariable->SetStringField(TEXT("variable_name"), TEXT("bCanInteract"));
			GetVariable->SetArrayField(TEXT("position"), MakePosition(340.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(GetVariable)));
			if (!CanInteractAnchors.ResultNodeGuid.IsEmpty())
			{
				AddConnectOperation(Operations, TEXT("can_interact_state"), CanInteractAnchors.ResultNodeGuid);
			}

			TSharedPtr<FJsonObject> GraphPayload;
			if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_can_interact"), Context, Steps, PartialResults, StepError, &GraphPayload))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
			FString CanInteractStateGuid;
			if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("can_interact_state"), CanInteractStateGuid, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
			}
			if (!ExecuteLayout(AssetPath, TEXT("CanInteract"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("CanInteract"), TEXT("Can Interact"), {CanInteractStateGuid}, TEXT("comment_can_interact"), Context, Steps, PartialResults, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
		}

		{
			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedPtr<FJsonObject> CallCanInteract = MakeOperation(TEXT("add_call_function"), TEXT("Interact"));
			CallCanInteract->SetStringField(TEXT("alias"), TEXT("call_can_interact"));
			CallCanInteract->SetStringField(TEXT("function_name"), TEXT("CanInteract"));
			CallCanInteract->SetArrayField(TEXT("position"), MakePosition(320.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(CallCanInteract)));

			TSharedPtr<FJsonObject> BranchNode = MakeOperation(TEXT("add_branch"), TEXT("Interact"));
			BranchNode->SetStringField(TEXT("alias"), TEXT("interaction_branch"));
			BranchNode->SetArrayField(TEXT("position"), MakePosition(660.0, 0.0));
			Operations.Add(MakeShareable(new FJsonValueObject(BranchNode)));

			TSharedPtr<FJsonObject> SuccessHook = MakeOperation(TEXT("add_reroute"), TEXT("Interact"));
			SuccessHook->SetStringField(TEXT("alias"), TEXT("interaction_success"));
			SuccessHook->SetArrayField(TEXT("position"), MakePosition(980.0, -140.0));
			Operations.Add(MakeShareable(new FJsonValueObject(SuccessHook)));

			TSharedPtr<FJsonObject> BlockedHook = MakeOperation(TEXT("add_reroute"), TEXT("Interact"));
			BlockedHook->SetStringField(TEXT("alias"), TEXT("interaction_blocked"));
			BlockedHook->SetArrayField(TEXT("position"), MakePosition(980.0, 140.0));
			Operations.Add(MakeShareable(new FJsonValueObject(BlockedHook)));

			AddPinConnectOperation(Operations, InteractAnchors.EntryNodeGuid, TEXT("then"), TEXT("interaction_branch"), TEXT("execute"));
			AddPinConnectOperation(Operations, InteractAnchors.EntryNodeGuid, TEXT("Interactor"), TEXT("call_can_interact"), TEXT("Interactor"));
			AddPinConnectOperation(Operations, TEXT("call_can_interact"), TEXT("ReturnValue"), TEXT("interaction_branch"), TEXT("Condition"));
			AddPinConnectOperation(Operations, TEXT("interaction_branch"), TEXT("then"), TEXT("interaction_success"), TEXT("InputPin"));
			AddPinConnectOperation(Operations, TEXT("interaction_branch"), TEXT("else"), TEXT("interaction_blocked"), TEXT("InputPin"));

			TSharedPtr<FJsonObject> GraphPayload;
			if (!ExecuteGraphBatch(AssetPath, Operations, TEXT("configure_interact"), Context, Steps, PartialResults, StepError, &GraphPayload))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
			FString SuccessGuid;
			FString BlockedGuid;
			if (!RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("interaction_success"), SuccessGuid, StepError)
				|| !RequireCreatedNodeGuidByAlias(GraphPayload, TEXT("interaction_blocked"), BlockedGuid, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), StepError);
			}
			if (!ExecuteLayout(AssetPath, TEXT("Interact"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("Interact"), TEXT("Success Path"), {SuccessGuid}, TEXT("comment_interact_success"), Context, Steps, PartialResults, StepError)
				|| !ExecuteCommentRegion(AssetPath, TEXT("Interact"), TEXT("Blocked Path"), {BlockedGuid}, TEXT("comment_interact_blocked"), Context, Steps, PartialResults, StepError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PATTERN_GRAPH_SETUP_FAILED"), StepError);
			}
		}
	}

	TSharedPtr<FJsonObject> CompilePayload;
	if (CompilePolicy.Equals(TEXT("final"), ESearchCase::IgnoreCase))
	{
		TSharedPtr<FJsonObject> CompileArgs = MakeShareable(new FJsonObject);
		CompileArgs->SetArrayField(TEXT("asset_paths"), { MakeStringValue(AssetPath) });
		CompileArgs->SetBoolField(TEXT("include_diagnostics"), true);
		CompileArgs->SetNumberField(TEXT("max_diagnostics"), 100);

		UCompileAssetsTool* CompileTool = NewObject<UCompileAssetsTool>();
		if (!InvokeStructuredTool(CompileTool, TEXT("compile_asset"), CompileArgs, Context, Steps, PartialResults, CompilePayload, StepError))
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("asset_path"), AssetPath);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR"), StepError, Details);
		}
	}

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("asset_path"), AssetPath);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError, Details);
		}

		TSharedPtr<FJsonObject> SavePayload = MakeShareable(new FJsonObject);
		SavePayload->SetStringField(TEXT("asset_path"), AssetPath);
		AddStep(TEXT("save_asset"), true, SavePayload, Steps);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("create-blueprint-pattern"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("pattern"), Pattern);
	Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, TEXT("Blueprint")));
	Response->SetArrayField(TEXT("steps"), Steps);
	Response->SetArrayField(TEXT("partial_results"), PartialResults);
	Response->SetArrayField(TEXT("modified_assets"), { MakeStringValue(AssetPath) });
	if (CompilePayload.IsValid())
	{
		Response->SetObjectField(TEXT("compile"), CompilePayload);
	}
	return FMcpToolResult::StructuredJson(Response);
}
