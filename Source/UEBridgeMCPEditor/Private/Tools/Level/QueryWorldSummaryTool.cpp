// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryWorldSummaryTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

FString UQueryWorldSummaryTool::GetToolDescription() const
{
	return TEXT("Return high-level world state, including actor counts, loaded levels, and current selection.");
}

TMap<FString, FMcpSchemaProperty> UQueryWorldSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), {TEXT("editor"), TEXT("pie"), TEXT("auto")}));
	Schema.Add(TEXT("include_levels"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include loaded level list")));
	Schema.Add(TEXT("include_selection"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include current editor selection")));
	return Schema;
}

FMcpToolResult UQueryWorldSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bIncludeLevels = GetBoolArgOrDefault(Arguments, TEXT("include_levels"), true);
	const bool bIncludeSelection = GetBoolArgOrDefault(Arguments, TEXT("include_selection"), true);

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	return FMcpToolResult::StructuredSuccess(
		McpV2ToolUtils::BuildWorldSummary(World, Context.SessionId, bIncludeLevels, bIncludeSelection),
		TEXT("World summary ready"));
}
