// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Animation/EditAnimBlueprintStateMachineTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Root.h"
#include "AnimationStateGraph.h"
#include "AnimationStateGraphSchema.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace
{
	UAnimGraphNode_StateMachineBase* FindStateMachineNode(UAnimBlueprint* AnimBlueprint, const FString& StateMachineName)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
				if (StateMachineNode && StateMachineNode->GetStateMachineName().Equals(StateMachineName, ESearchCase::IgnoreCase))
				{
					return StateMachineNode;
				}
			}
		}

		return nullptr;
	}

	UAnimationGraph* FindPrimaryAnimationGraph(UAnimBlueprint* AnimBlueprint)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> AllGraphs;
		AnimBlueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(Graph);
			if (!AnimationGraph)
			{
				continue;
			}

			TArray<UAnimGraphNode_Root*> RootNodes;
			AnimationGraph->GetNodesOfClass<UAnimGraphNode_Root>(RootNodes);
			if (RootNodes.Num() > 0)
			{
				return AnimationGraph;
			}
		}
		return nullptr;
	}

	UAnimGraphNode_Root* FindRootNode(UAnimationGraph* AnimationGraph)
	{
		if (!AnimationGraph)
		{
			return nullptr;
		}

		TArray<UAnimGraphNode_Root*> RootNodes;
		AnimationGraph->GetNodesOfClass<UAnimGraphNode_Root>(RootNodes);
		return RootNodes.Num() > 0 ? RootNodes[0] : nullptr;
	}

	UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* StateMachineGraph, const FString& StateName)
	{
		if (!StateMachineGraph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
			if (StateNode && StateNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
			{
				return StateNode;
			}
		}
		return nullptr;
	}

	UAnimStateTransitionNode* FindTransitionNode(UAnimationStateMachineGraph* StateMachineGraph, const FString& FromStateName, const FString& ToStateName)
	{
		if (!StateMachineGraph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : StateMachineGraph->Nodes)
		{
			UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
			if (!TransitionNode)
			{
				continue;
			}

			const UAnimStateNodeBase* PreviousState = TransitionNode->GetPreviousState();
			const UAnimStateNodeBase* NextState = TransitionNode->GetNextState();
			if (PreviousState && NextState &&
				PreviousState->GetStateName().Equals(FromStateName, ESearchCase::IgnoreCase) &&
				NextState->GetStateName().Equals(ToStateName, ESearchCase::IgnoreCase))
			{
				return TransitionNode;
			}
		}

		return nullptr;
	}

	UEdGraphPin* FindFirstPoseOutputPin(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindFirstPoseInputPin(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UAnimGraphNode_StateMachine* CreateStateMachineNode(
		UAnimBlueprint* AnimBlueprint,
		const FString& StateMachineName,
		const double X,
		const double Y,
		const bool bConnectToOutput,
		FString& OutError)
	{
		UAnimationGraph* AnimationGraph = FindPrimaryAnimationGraph(AnimBlueprint);
		if (!AnimationGraph)
		{
			OutError = TEXT("AnimBlueprint has no primary AnimGraph with a root node");
			return nullptr;
		}

		FGraphNodeCreator<UAnimGraphNode_StateMachine> NodeCreator(*AnimationGraph);
		UAnimGraphNode_StateMachine* StateMachineNode = NodeCreator.CreateNode();
		NodeCreator.Finalize();
		if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
		{
			OutError = TEXT("Failed to create state machine node");
			return nullptr;
		}

		StateMachineNode->NodePosX = static_cast<int32>(X);
		StateMachineNode->NodePosY = static_cast<int32>(Y);
		StateMachineNode->OnRenameNode(StateMachineName);

		if (bConnectToOutput)
		{
			UAnimGraphNode_Root* RootNode = FindRootNode(AnimationGraph);
			UEdGraphPin* OutputPin = FindFirstPoseOutputPin(StateMachineNode);
			UEdGraphPin* InputPin = FindFirstPoseInputPin(RootNode);
			const UEdGraphSchema* Schema = AnimationGraph->GetSchema();
			if (!RootNode || !OutputPin || !InputPin || !Schema)
			{
				OutError = TEXT("Failed to resolve AnimGraph output pins for state machine connection");
				return nullptr;
			}

			Schema->BreakPinLinks(*InputPin, true);
			Schema->TryCreateConnection(OutputPin, InputPin);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		return StateMachineNode;
	}

	UAnimGraphNode_SequencePlayer* EnsureSequencePlayerNode(UAnimStateNode* StateNode, UAnimSequence* SequenceAsset)
	{
		if (!StateNode || !StateNode->BoundGraph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : StateNode->BoundGraph->Nodes)
		{
			if (UAnimGraphNode_SequencePlayer* SequencePlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				SequencePlayer->Modify();
				SequencePlayer->SetAnimationAsset(SequenceAsset);
				return SequencePlayer;
			}
		}

		FGraphNodeCreator<UAnimGraphNode_SequencePlayer> NodeCreator(*StateNode->BoundGraph);
		UAnimGraphNode_SequencePlayer* SequencePlayer = NodeCreator.CreateNode();
		NodeCreator.Finalize();
		SequencePlayer->NodePosX = 0;
		SequencePlayer->NodePosY = 0;
		SequencePlayer->SetAnimationAsset(SequenceAsset);

		UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(StateNode->BoundGraph);
		if (StateGraph && StateGraph->GetResultNode())
		{
			UEdGraphPin* OutputPin = FindFirstPoseOutputPin(SequencePlayer);
			UEdGraphPin* InputPin = FindFirstPoseInputPin(StateGraph->GetResultNode());
			if (OutputPin && InputPin && StateNode->BoundGraph->GetSchema())
			{
				StateNode->BoundGraph->GetSchema()->TryCreateConnection(OutputPin, InputPin);
			}
		}

		return SequencePlayer;
	}
}

FString UEditAnimBlueprintStateMachineTool::GetToolDescription() const
{
	return TEXT("Edit an existing Anim Blueprint state machine with batched structural operations such as add/remove state, transitions, entry state, and single-sequence state assignment.");
}

TMap<FString, FMcpSchemaProperty> UEditAnimBlueprintStateMachineTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Anim Blueprint asset path"), true));
	Schema.Add(TEXT("state_machine_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing state machine name"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("State machine edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("State machine action"),
		{ TEXT("create_state_machine"), TEXT("ensure_state_machine"), TEXT("add_state"), TEXT("remove_state"), TEXT("rename_state"), TEXT("set_entry_state"), TEXT("add_transition"), TEXT("remove_transition"), TEXT("set_state_sequence") },
		true)));
	OperationSchema->Properties.Add(TEXT("state_machine_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional state machine name override for create_state_machine or ensure_state_machine"))));
	OperationSchema->Properties.Add(TEXT("state_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing or new state name"))));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Replacement state name"))));
	OperationSchema->Properties.Add(TEXT("from_state"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transition source state name"))));
	OperationSchema->Properties.Add(TEXT("to_state"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transition target state name"))));
	OperationSchema->Properties.Add(TEXT("sequence_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("AnimSequence asset path for set_state_sequence"))));
	OperationSchema->Properties.Add(TEXT("crossfade_duration"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional transition crossfade duration"))));
	OperationSchema->Properties.Add(TEXT("x"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional node X position"))));
	OperationSchema->Properties.Add(TEXT("y"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional node Y position"))));
	OperationSchema->Properties.Add(TEXT("connect_to_output"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("For create_state_machine, connect the new state machine to the AnimGraph output. Default: true"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("State machine operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the Anim Blueprint asset")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile the Anim Blueprint after edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

FMcpToolResult UEditAnimBlueprintStateMachineTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString StateMachineName = GetStringArgOrDefault(Arguments, TEXT("state_machine_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UAnimBlueprint* AnimBlueprint = FMcpAssetModifier::LoadAssetByPath<UAnimBlueprint>(AssetPath, LoadError);
	if (!AnimBlueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Anim Blueprint State Machine")));
		AnimBlueprint->Modify();
	}

	UAnimGraphNode_StateMachineBase* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bOperationSuccess = false;
		bool bOperationChanged = false;
		FString OperationError;

		if (Action == TEXT("create_state_machine") || Action == TEXT("ensure_state_machine"))
		{
			FString OperationStateMachineName = StateMachineName;
			(*OperationObject)->TryGetStringField(TEXT("state_machine_name"), OperationStateMachineName);
			if (OperationStateMachineName.IsEmpty())
			{
				OperationError = TEXT("'state_machine_name' or top-level 'state_machine_name' is required");
			}
			else if (UAnimGraphNode_StateMachineBase* ExistingStateMachineNode = FindStateMachineNode(AnimBlueprint, OperationStateMachineName))
			{
				StateMachineNode = ExistingStateMachineNode;
				if (Action == TEXT("create_state_machine"))
				{
					OperationError = TEXT("State machine already exists");
				}
				else
				{
					bOperationSuccess = true;
					bOperationChanged = false;
					ResultObject->SetStringField(TEXT("state_machine_name"), OperationStateMachineName);
					if (ExistingStateMachineNode->EditorStateMachineGraph)
					{
						ResultObject->SetStringField(TEXT("graph_name"), ExistingStateMachineNode->EditorStateMachineGraph->GetName());
					}
				}
			}
			else if (!bDryRun)
			{
				double X = -300.0;
				double Y = 0.0;
				(*OperationObject)->TryGetNumberField(TEXT("x"), X);
				(*OperationObject)->TryGetNumberField(TEXT("y"), Y);
				bool bConnectToOutput = true;
				(*OperationObject)->TryGetBoolField(TEXT("connect_to_output"), bConnectToOutput);
				StateMachineNode = CreateStateMachineNode(AnimBlueprint, OperationStateMachineName, X, Y, bConnectToOutput, OperationError);
				if (StateMachineNode)
				{
					bOperationSuccess = true;
					bOperationChanged = true;
					ResultObject->SetStringField(TEXT("state_machine_name"), OperationStateMachineName);
					ResultObject->SetStringField(TEXT("node_guid"), StateMachineNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
					if (StateMachineNode->EditorStateMachineGraph)
					{
						ResultObject->SetStringField(TEXT("graph_name"), StateMachineNode->EditorStateMachineGraph->GetName());
					}
				}
			}
			else
			{
				bOperationSuccess = true;
				bOperationChanged = true;
				ResultObject->SetStringField(TEXT("state_machine_name"), OperationStateMachineName);
			}
		}
		else if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
		{
			OperationError = TEXT("State machine not found; run create_state_machine or ensure_state_machine first");
		}
		else if (Action == TEXT("add_state"))
		{
			FString StateName;
			if (!(*OperationObject)->TryGetStringField(TEXT("state_name"), StateName) || StateName.IsEmpty())
			{
				OperationError = TEXT("'state_name' is required for add_state");
			}
			else if (FindStateNode(StateMachineNode->EditorStateMachineGraph, StateName))
			{
				OperationError = TEXT("State already exists");
			}
			else if (!bDryRun)
			{
					double X = 0.0;
					double Y = 0.0;
					(*OperationObject)->TryGetNumberField(TEXT("x"), X);
					(*OperationObject)->TryGetNumberField(TEXT("y"), Y);
					UAnimStateNode* StateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
						StateMachineNode->EditorStateMachineGraph, NewObject<UAnimStateNode>(), FVector2f(X, Y), false);
				if (!StateNode)
				{
					OperationError = TEXT("Failed to create state node");
				}
				else
				{
					StateNode->OnRenameNode(StateName);
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
			else
			{
				bOperationSuccess = true;
				bOperationChanged = true;
			}
		}
		else if (Action == TEXT("remove_state"))
		{
			FString StateName;
			if (!(*OperationObject)->TryGetStringField(TEXT("state_name"), StateName) || StateName.IsEmpty())
			{
				OperationError = TEXT("'state_name' is required for remove_state");
			}
			else if (UAnimStateNode* StateNode = FindStateNode(StateMachineNode->EditorStateMachineGraph, StateName))
			{
				if (!bDryRun)
				{
					FBlueprintEditorUtils::RemoveNode(AnimBlueprint, StateNode, true);
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
				}
				bOperationSuccess = true;
				bOperationChanged = true;
			}
			else
			{
				OperationError = TEXT("State not found");
			}
		}
		else if (Action == TEXT("rename_state"))
		{
			FString StateName;
			FString NewName;
			if (!(*OperationObject)->TryGetStringField(TEXT("state_name"), StateName) || !(*OperationObject)->TryGetStringField(TEXT("new_name"), NewName) || StateName.IsEmpty() || NewName.IsEmpty())
			{
				OperationError = TEXT("'state_name' and 'new_name' are required for rename_state");
			}
			else if (FindStateNode(StateMachineNode->EditorStateMachineGraph, NewName))
			{
				OperationError = TEXT("A state with the new name already exists");
			}
			else if (UAnimStateNode* StateNode = FindStateNode(StateMachineNode->EditorStateMachineGraph, StateName))
			{
				if (!bDryRun)
				{
					StateNode->Modify();
					StateNode->OnRenameNode(NewName);
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
				}
				bOperationSuccess = true;
				bOperationChanged = true;
			}
			else
			{
				OperationError = TEXT("State not found");
			}
		}
		else if (Action == TEXT("set_entry_state"))
		{
			FString StateName;
			if (!(*OperationObject)->TryGetStringField(TEXT("state_name"), StateName) || StateName.IsEmpty())
			{
				OperationError = TEXT("'state_name' is required for set_entry_state");
			}
			else if (UAnimStateNode* StateNode = FindStateNode(StateMachineNode->EditorStateMachineGraph, StateName))
			{
				if (!bDryRun)
				{
					const UAnimationStateMachineSchema* Schema = Cast<UAnimationStateMachineSchema>(StateMachineNode->EditorStateMachineGraph->GetSchema());
					UEdGraphPin* EntryPin = StateMachineNode->EditorStateMachineGraph->EntryNode && StateMachineNode->EditorStateMachineGraph->EntryNode->Pins.Num() > 0
						? StateMachineNode->EditorStateMachineGraph->EntryNode->Pins[0]
						: nullptr;
					if (!Schema || !EntryPin || !StateNode->GetInputPin())
					{
						OperationError = TEXT("Failed to resolve entry connection pins");
					}
					else
					{
						Schema->BreakPinLinks(*EntryPin, true);
						Schema->TryCreateConnection(EntryPin, StateNode->GetInputPin());
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
					}
				}
				if (OperationError.IsEmpty())
				{
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
			else
			{
				OperationError = TEXT("State not found");
			}
		}
		else if (Action == TEXT("add_transition"))
		{
			FString FromState;
			FString ToState;
			if (!(*OperationObject)->TryGetStringField(TEXT("from_state"), FromState) || !(*OperationObject)->TryGetStringField(TEXT("to_state"), ToState) || FromState.IsEmpty() || ToState.IsEmpty())
			{
				OperationError = TEXT("'from_state' and 'to_state' are required for add_transition");
			}
			else if (FindTransitionNode(StateMachineNode->EditorStateMachineGraph, FromState, ToState))
			{
				OperationError = TEXT("Transition already exists");
			}
			else
			{
				UAnimStateNode* PreviousState = FindStateNode(StateMachineNode->EditorStateMachineGraph, FromState);
				UAnimStateNode* NextState = FindStateNode(StateMachineNode->EditorStateMachineGraph, ToState);
				if (!PreviousState || !NextState)
				{
					OperationError = TEXT("Both from_state and to_state must exist");
				}
				else if (!bDryRun)
				{
					UAnimStateTransitionNode* TransitionNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(
						StateMachineNode->EditorStateMachineGraph, NewObject<UAnimStateTransitionNode>(), FVector2f(0.0f, 0.0f), false);
					if (!TransitionNode)
					{
						OperationError = TEXT("Failed to create transition node");
					}
					else
					{
						TransitionNode->CreateConnections(PreviousState, NextState);
						double CrossfadeDuration = 0.0;
						if ((*OperationObject)->TryGetNumberField(TEXT("crossfade_duration"), CrossfadeDuration))
						{
							TransitionNode->CrossfadeDuration = static_cast<float>(CrossfadeDuration);
						}
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
				else
				{
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
		}
		else if (Action == TEXT("remove_transition"))
		{
			FString FromState;
			FString ToState;
			if (!(*OperationObject)->TryGetStringField(TEXT("from_state"), FromState) || !(*OperationObject)->TryGetStringField(TEXT("to_state"), ToState) || FromState.IsEmpty() || ToState.IsEmpty())
			{
				OperationError = TEXT("'from_state' and 'to_state' are required for remove_transition");
			}
			else if (UAnimStateTransitionNode* TransitionNode = FindTransitionNode(StateMachineNode->EditorStateMachineGraph, FromState, ToState))
			{
				if (!bDryRun)
				{
					FBlueprintEditorUtils::RemoveNode(AnimBlueprint, TransitionNode, true);
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
				}
				bOperationSuccess = true;
				bOperationChanged = true;
			}
			else
			{
				OperationError = TEXT("Transition not found");
			}
		}
		else if (Action == TEXT("set_state_sequence"))
		{
			FString StateName;
			FString SequencePath;
			if (!(*OperationObject)->TryGetStringField(TEXT("state_name"), StateName) || !(*OperationObject)->TryGetStringField(TEXT("sequence_path"), SequencePath) || StateName.IsEmpty() || SequencePath.IsEmpty())
			{
				OperationError = TEXT("'state_name' and 'sequence_path' are required for set_state_sequence");
			}
			else if (UAnimStateNode* StateNode = FindStateNode(StateMachineNode->EditorStateMachineGraph, StateName))
			{
				FString SequenceLoadError;
				UAnimSequence* Sequence = FMcpAssetModifier::LoadAssetByPath<UAnimSequence>(SequencePath, SequenceLoadError);
				if (!Sequence)
				{
					OperationError = SequenceLoadError;
				}
				else if (!bDryRun)
				{
					UAnimGraphNode_SequencePlayer* SequencePlayer = EnsureSequencePlayerNode(StateNode, Sequence);
					if (!SequencePlayer)
					{
						OperationError = TEXT("Failed to create or update sequence player node");
					}
					else
					{
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
				else
				{
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
			else
			{
				OperationError = TEXT("State not found");
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *Action);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!OperationError.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("error"), OperationError);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;

			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					OperationError,
					nullptr,
					GameplayToolUtils::BuildBatchFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}
		else
		{
			bChanged = bChanged || bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(AnimBlueprint);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));

		if (bCompile)
		{
			FString CompileError;
			if (!FMcpAssetModifier::CompileBlueprint(AnimBlueprint, CompileError))
			{
				if (Transaction.IsValid() && bRollbackOnError)
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
			}
		}

		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(AnimBlueprint, false, SaveError))
			{
				if (Transaction.IsValid() && bRollbackOnError)
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
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
