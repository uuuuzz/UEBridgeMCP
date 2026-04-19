// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryWorldTool.generated.h"

/**
 * 世界状态查询工具 — 查询编辑器或 PIE 世界中的 Actor、组件、属性等。
 * 是 pie-session、pie-input、call-function 的验证伙伴。
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryWorldTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-world"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	TSharedPtr<FJsonObject> SerializeActor(AActor* Actor, bool bIncludeComponents, bool bIncludeTransform,
		bool bIncludeProperties, bool bIncludeTags, bool bIncludeBounds) const;
	TSharedPtr<FJsonObject> SerializeComponent(UActorComponent* Component, bool bIncludeProperties) const;
};
