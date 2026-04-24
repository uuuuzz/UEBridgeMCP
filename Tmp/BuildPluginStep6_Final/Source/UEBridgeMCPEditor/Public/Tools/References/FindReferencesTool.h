// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "AssetRegistry/AssetData.h"
#include "FindReferencesTool.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class FStreamSearch;
struct FSearchData;

/**
 * Tool for finding references to assets, Blueprint variables, and nodes.
 *
 * Supports three reference types:
 * - "asset": Find all assets that reference the given asset
 * - "property": Find where a Blueprint variable is used within its own graphs
 * - "node": Find all usages of a specific node type or function call
 *
 * Uses UE5's Find in Blueprints (FiB) cache for faster searching when available.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UFindReferencesTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("find-references"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	/** Find assets that reference the given asset */
	FMcpToolResult FindAssetReferences(
		const FString& AssetPath,
		const FString& Direction,
		int32 MaxDepth,
		const FString& PathFilter,
		const FString& OutputPath,
		int32 Limit);

	/** Find usages of a variable within a Blueprint */
	FMcpToolResult FindPropertyReferences(const FString& AssetPath, const FString& VariableName, int32 Limit);

	/** Find usages of a node type or function within a Blueprint - uses FiB cache */
	FMcpToolResult FindNodeReferences(const FString& AssetPath, const FString& NodeClass,
									   const FString& FunctionName, int32 Limit);

	/** Find usages of a node type or function - legacy method loading each Blueprint */
	FMcpToolResult FindNodeReferencesLegacy(const FString& AssetPath, const FString& NodeClass,
											const FString& FunctionName, int32 Limit);

	/** Use FiB cache to quickly find matching Blueprints, then load for detailed inspection */
	TArray<FSoftObjectPath> FindMatchingBlueprintsViaFiB(const FString& SearchTerm, const FString& PathFilter);

	/** Helper to get all graphs from a Blueprint */
	TArray<UEdGraph*> GetAllGraphs(UBlueprint* Blueprint) const;

	/** Helper to determine graph type string */
	FString GetGraphType(UBlueprint* Blueprint, UEdGraph* Graph) const;

	/** Convert a node reference to JSON */
	TSharedPtr<FJsonObject> NodeReferenceToJson(UEdGraphNode* Node, UEdGraph* Graph,
												 const FString& GraphType) const;

	/** Get all Blueprints in a directory path */
	TArray<FAssetData> GetBlueprintsInPath(const FString& Path) const;
};
