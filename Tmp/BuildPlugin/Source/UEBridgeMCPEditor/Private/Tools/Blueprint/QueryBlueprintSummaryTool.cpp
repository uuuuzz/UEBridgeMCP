// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/QueryBlueprintSummaryTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpV2ToolUtils.h"

FString UQueryBlueprintSummaryTool::GetToolDescription() const
{
	return TEXT("Return a compact Blueprint summary with counts for graphs, functions, variables, and components.");
}

TMap<FString, FMcpSchemaProperty> UQueryBlueprintSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("include_names"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include function, variable, and component names")));
	return Schema;
}

TArray<FString> UQueryBlueprintSummaryTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult UQueryBlueprintSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeNames = GetBoolArgOrDefault(Arguments, TEXT("include_names"), false);

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	return FMcpToolResult::StructuredSuccess(
		McpV2ToolUtils::BuildBlueprintSummary(Blueprint, AssetPath, bIncludeNames),
		TEXT("Blueprint summary ready"));
}
