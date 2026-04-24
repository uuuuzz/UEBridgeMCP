// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditWidgetAnimationTool.generated.h"

/**
 * Minimal Widget Blueprint animation list editing.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UEditWidgetAnimationTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-widget-animation"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("edit"); }
	virtual FString GetResourceScope() const override { return TEXT("ui"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsBatch() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsCompile() const override { return true; }
	virtual bool SupportsSave() const override { return true; }

	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path"), TEXT("operations") }; }

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
};
