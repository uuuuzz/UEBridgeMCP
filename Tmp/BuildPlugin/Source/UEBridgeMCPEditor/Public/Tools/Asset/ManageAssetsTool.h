// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "ManageAssetsTool.generated.h"

/**
 * 资产管理工具 — 支持重命名、移动、复制、删除、保存资产。
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UManageAssetsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("manage-assets"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	bool ExecuteAction(const TSharedPtr<FJsonObject>& Action, int32 Index, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
};
