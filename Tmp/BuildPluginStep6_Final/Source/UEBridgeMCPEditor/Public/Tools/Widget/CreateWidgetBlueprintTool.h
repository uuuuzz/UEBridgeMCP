// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CreateWidgetBlueprintTool.generated.h"

/**
 * Create a Widget Blueprint with an optional default root widget.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UCreateWidgetBlueprintTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("create-widget-blueprint"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("create"); }
	virtual FString GetResourceScope() const override { return TEXT("ui"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }

	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path") }; }

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
};
