// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/EditMaterialInstanceBatchTool.h"

#include "Tools/Material/EditMaterialInstanceTool.h"
#include "Tools/McpToolRegistry.h"
#include "UEBridgeMCP.h"

FString UEditMaterialInstanceBatchTool::GetToolDescription() const
{
	return TEXT("Batch material instance editing (alias of 'edit-material-instance' exposed with batch metadata). Supports dry-run and save.");
}

TMap<FString, FMcpSchemaProperty> UEditMaterialInstanceBatchTool::GetInputSchema() const
{
	return GetDefault<UEditMaterialInstanceTool>()->GetInputSchema();
}

TArray<FString> UEditMaterialInstanceBatchTool::GetRequiredParams() const
{
	return GetDefault<UEditMaterialInstanceTool>()->GetRequiredParams();
}

FMcpToolResult UEditMaterialInstanceBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	// P0-H3: 使用 Registry 中已预热、已 AddToRoot 的单例，避免 NewObject 后被 GC。
	UMcpToolBase* InnerTool = FMcpToolRegistry::Get().FindTool(TEXT("edit-material-instance"));
	if (!InnerTool)
	{
		UE_LOG(LogUEBridgeMCP, Error, TEXT("edit-material-instance-batch: inner tool 'edit-material-instance' not registered"));
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INNER_TOOL_MISSING"),
			TEXT("Inner tool 'edit-material-instance' is not registered"));
	}
	return InnerTool->Execute(Arguments, Context);
}
