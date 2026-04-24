// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditWidgetLayoutBatchTool.generated.h"

/**
 * Layout-focused alias for edit-widget-blueprint.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UEditWidgetLayoutBatchTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-widget-layout-batch"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("edit"); }
	virtual FString GetResourceScope() const override { return TEXT("ui"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsBatch() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsCompile() const override { return true; }
	virtual bool SupportsSave() const override { return true; }

	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
};
