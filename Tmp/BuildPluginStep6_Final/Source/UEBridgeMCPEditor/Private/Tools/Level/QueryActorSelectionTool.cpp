// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryActorSelectionTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

FString UQueryActorSelectionTool::GetToolDescription() const
{
	return TEXT("Return the current actor selection for the resolved world, with optional transform and bounds data.");
}

TMap<FString, FMcpSchemaProperty> UQueryActorSelectionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("include_transform"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actor transforms in selection results")));
	Schema.Add(TEXT("include_bounds"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actor bounds in selection results")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum selected actors to return")));
	return Schema;
}

FMcpToolResult UQueryActorSelectionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
	const bool bIncludeBounds = GetBoolArgOrDefault(Arguments, TEXT("include_bounds"), true);
	const int32 Limit = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("limit"), 200));

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	TSharedPtr<FJsonObject> Result = McpV2ToolUtils::BuildSelectionSummary(World, Context.SessionId, bIncludeTransform, bIncludeBounds, Limit);
	if (!Result.IsValid())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), TEXT("Failed to build actor selection summary"));
	}

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Actor selection ready"));
}
