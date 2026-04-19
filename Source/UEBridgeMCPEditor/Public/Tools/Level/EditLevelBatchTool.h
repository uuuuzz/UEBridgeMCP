// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditLevelBatchTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UEditLevelBatchTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-level-batch"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("batch"); }
	virtual FString GetResourceScope() const override { return TEXT("level"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsBatch() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
};
