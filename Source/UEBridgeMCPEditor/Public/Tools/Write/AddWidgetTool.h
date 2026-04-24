// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "AddWidgetTool.generated.h"

/**
 * Add a widget to a WidgetBlueprint.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UAddWidgetTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("add-widget"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("write"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool RequiresGameThread() const override { return true; }
};
