// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CommonUITools.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UCreateCommonUIWidgetTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("create-common-ui-widget"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("create"); }
	virtual FString GetResourceScope() const override { return TEXT("widget_blueprint"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path"), TEXT("commonui_type") }; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};

UCLASS()
class UEBRIDGEMCPEDITOR_API UEditCommonUITool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-common-ui"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("batch"); }
	virtual FString GetResourceScope() const override { return TEXT("widget_blueprint"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsBatch() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path"), TEXT("operations") }; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryCommonUIWidgetsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-common-ui-widgets"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetResourceScope() const override { return TEXT("widget_blueprint"); }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
