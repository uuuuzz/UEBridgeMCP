// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryBlueprintGraphTool.generated.h"

/**
 * Consolidated tool for Blueprint graph inspection.
 * Replaces: get-blueprint-graph, get-blueprint-node, list-blueprint-callables, get-callable-details
 *
 * Usage modes:
 * - No special params: List all graphs with nodes (like get-blueprint-graph)
 * - node_guid: Get specific node details (like get-blueprint-node)
 * - callable_name: Get specific callable's graph (like get-callable-details)
 * - list_callables=true: List all callables without full graphs (like list-blueprint-callables)
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryBlueprintGraphTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-blueprint-graph"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	// === Graph conversion ===

	/** Convert a graph to JSON */
	TSharedPtr<FJsonObject> GraphToJson(class UEdGraph* Graph, const FString& GraphType, bool bIncludePositions) const;

	/** Convert a node to JSON */
	TSharedPtr<FJsonObject> NodeToJson(class UEdGraphNode* Node, bool bIncludePositions) const;

	/** Convert a pin to JSON */
	TSharedPtr<FJsonObject> PinToJson(class UEdGraphPin* Pin) const;

	// === Node search ===

	/** Find a node by GUID across all graphs */
	class UEdGraphNode* FindNodeByGuid(class UBlueprint* Blueprint, const FGuid& NodeGuid, FString& OutGraphName, FString& OutGraphType) const;

	/** Find a graph by name */
	class UEdGraph* FindGraphByName(class UBlueprint* Blueprint, const FString& CallableName, FString& OutGraphType) const;

	// === Callable extraction ===

	/** Extract events from event graphs */
	void ExtractEvents(class UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	/** Extract functions */
	void ExtractFunctions(class UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	/** Extract macros */
	void ExtractMacros(class UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	// === Helpers ===

	/** Get pin category as string */
	FString GetPinCategoryString(FName Category) const;

	/** Get pin direction as string */
	FString GetPinDirectionString(EEdGraphPinDirection Direction) const;

	// === Animation Blueprint support ===

	/** Get animation graph type string from graph class */
	FString GetAnimGraphTypeString(class UEdGraph* Graph) const;

	/** Process animation-specific graphs from AnimBlueprint */
	void ProcessAnimBlueprintGraphs(
		class UAnimBlueprint* AnimBP,
		const FString& GraphNameFilter,
		const FString& GraphTypeFilter,
		bool bIncludePositions,
		TArray<TSharedPtr<FJsonValue>>& OutGraphsArray) const;

	/** Extract state machine hierarchy (states, transitions, conduits) */
	void ExtractStateMachineHierarchy(
		class UAnimationStateMachineGraph* StateMachineGraph,
		TSharedPtr<FJsonObject>& OutGraphJson,
		bool bIncludePositions) const;

	/** Convert AnimStateNode to JSON with state-specific data */
	TSharedPtr<FJsonObject> AnimStateNodeToJson(class UAnimStateNode* StateNode, bool bIncludePositions) const;

	/** Convert transition node to JSON with transition-specific data */
	TSharedPtr<FJsonObject> TransitionNodeToJson(class UAnimStateTransitionNode* TransitionNode, bool bIncludePositions) const;

	/** Find node by GUID in animation-specific graphs */
	class UEdGraphNode* FindNodeInAnimGraphs(
		class UAnimBlueprint* AnimBP,
		const FGuid& NodeGuid,
		FString& OutGraphName,
		FString& OutGraphType) const;

	/** Find animation graph by name */
	class UEdGraph* FindAnimGraphByName(
		class UAnimBlueprint* AnimBP,
		const FString& GraphName,
		FString& OutGraphType) const;

	/** Extract AnimBlueprint callables (AnimGraph entry points, state machines) */
	void ExtractAnimCallables(class UAnimBlueprint* AnimBP, TArray<TSharedPtr<FJsonValue>>& OutArray) const;
};
