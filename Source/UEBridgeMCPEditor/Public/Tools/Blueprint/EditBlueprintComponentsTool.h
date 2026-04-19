// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditBlueprintComponentsTool.generated.h"

class UBlueprint;
class USCS_Node;

/**
 * 蓝图组件树（SCS）编辑工具 — 支持添加、删除、重命名、重新挂接组件，以及设置组件默认值。
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UEditBlueprintComponentsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-blueprint-components"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	bool ExecuteAction(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, int32 Index,
		TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool AddComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool RemoveComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool RenameComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool AttachComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetComponentDefaults(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetRootComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	static USCS_Node* FindSCSNode(UBlueprint* Blueprint, const FString& ComponentName);
};
