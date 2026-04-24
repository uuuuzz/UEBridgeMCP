// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/QueryBlueprintGraphSummaryTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpV2ToolUtils.h"

FString UQueryBlueprintGraphSummaryTool::GetToolDescription() const
{
	return TEXT("Return compact graph summaries for a Blueprint, with optional sample node handles.");
}

TMap<FString, FMcpSchemaProperty> UQueryBlueprintGraphSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("graph_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional graph name filter")));
	Schema.Add(TEXT("graph_type"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional graph type filter")));
	Schema.Add(TEXT("include_sample_nodes"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include a small sample of node handles")));
	Schema.Add(TEXT("max_sample_nodes"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum sample nodes per graph")));
	return Schema;
}

TArray<FString> UQueryBlueprintGraphSummaryTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult UQueryBlueprintGraphSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString GraphNameFilter = GetStringArgOrDefault(Arguments, TEXT("graph_name"));
	const FString GraphTypeFilter = GetStringArgOrDefault(Arguments, TEXT("graph_type"));
	const bool bIncludeSampleNodes = GetBoolArgOrDefault(Arguments, TEXT("include_sample_nodes"), true);
	const int32 MaxSampleNodes = GetIntArgOrDefault(Arguments, TEXT("max_sample_nodes"), 5);

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	return FMcpToolResult::StructuredSuccess(
		McpV2ToolUtils::BuildBlueprintGraphSummary(Blueprint, AssetPath, Context.SessionId, GraphNameFilter, GraphTypeFilter, bIncludeSampleNodes, MaxSampleNodes),
		TEXT("Blueprint graph summary ready"));
}
