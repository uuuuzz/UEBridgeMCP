// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditLevelActorTool.generated.h"

/**
 * 关卡 Actor 综合编辑工具 — 支持 spawn、delete、duplicate、set_transform、
 * attach/detach、set_folder、set_label、add/remove_component 等操作。
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UEditLevelActorTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-level-actor"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	bool ExecuteAction(UWorld* World, const TSharedPtr<FJsonObject>& Action, int32 Index,
		TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool SpawnActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool DeleteActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool DuplicateActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetTransform(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool AttachActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool DetachActor(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetFolder(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetLabel(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool AddActorComponent(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool RemoveActorComponent(UWorld* World, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
};
