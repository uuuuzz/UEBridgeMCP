// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/EditLevelBatchTool.h"

#include "Tools/Level/EditLevelActorTool.h"
#include "Tools/McpToolRegistry.h"
#include "UEBridgeMCP.h"

FString UEditLevelBatchTool::GetToolDescription() const
{
	return TEXT("Batch level editing (alias of 'edit-level-actor' exposed with batch metadata so clients can route multi-actor ops here). Transactional, supports dry-run and save.");
}

TMap<FString, FMcpSchemaProperty> UEditLevelBatchTool::GetInputSchema() const
{
	return GetDefault<UEditLevelActorTool>()->GetInputSchema();
}

TArray<FString> UEditLevelBatchTool::GetRequiredParams() const
{
	return GetDefault<UEditLevelActorTool>()->GetRequiredParams();
}

FMcpToolResult UEditLevelBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	// P0-H3: 使用 Registry 中已预热、已 AddToRoot 的单例，避免 NewObject 后被 GC。
	UMcpToolBase* InnerTool = FMcpToolRegistry::Get().FindTool(TEXT("edit-level-actor"));
	if (!InnerTool)
	{
		UE_LOG(LogUEBridgeMCP, Error, TEXT("edit-level-batch: inner tool 'edit-level-actor' not registered"));
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INNER_TOOL_MISSING"),
			TEXT("Inner tool 'edit-level-actor' is not registered"));
	}
	return InnerTool->Execute(Arguments, Context);
}
