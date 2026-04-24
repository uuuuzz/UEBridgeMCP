// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/EditMaterialInstanceBatchTool.h"

#include "Tools/Material/EditMaterialInstanceTool.h"

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
	return GetMutableDefault<UEditMaterialInstanceTool>()->Execute(Arguments, Context);
}
