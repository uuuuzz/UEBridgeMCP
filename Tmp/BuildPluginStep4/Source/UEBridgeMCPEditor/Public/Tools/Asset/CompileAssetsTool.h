// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CompileAssetsTool.generated.h"

/**
 * 蓝图编译工具 — 支持批量编译 Blueprint、WidgetBlueprint、AnimBlueprint。
 * 替代通过 run-python-script 进行编译的旧方式。
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UCompileAssetsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("compile-assets"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }
};
