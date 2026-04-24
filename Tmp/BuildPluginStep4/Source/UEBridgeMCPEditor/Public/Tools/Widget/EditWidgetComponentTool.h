// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditWidgetComponentTool.generated.h"

/**
 * Edit WidgetComponent defaults on an actor instance or Blueprint component template.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UEditWidgetComponentTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-widget-component"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("edit"); }
	virtual FString GetResourceScope() const override { return TEXT("ui"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsBatch() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }

	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("operations") }; }

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
};
