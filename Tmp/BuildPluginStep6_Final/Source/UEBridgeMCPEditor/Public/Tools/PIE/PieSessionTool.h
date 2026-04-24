// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "Editor.h"
#include "PieSessionTool.generated.h"

/**
 * Consolidated PIE session control tool.
 * Actions: start, stop, pause, resume, get-state, wait-for
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UPieSessionTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("pie-session"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("action")}; }
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * 检查 PIE 是否正处于过渡状态（正在启动或停止）。
	 * 其他工具在执行前应检查此状态，避免在 PIE 过渡期间访问正在创建/销毁的世界对象。
	 */
	static bool IsPIETransitioning() { return bPIETransitioning; }

private:
	FMcpToolResult ExecuteStart(const TSharedPtr<FJsonObject>& Arguments);
	FMcpToolResult ExecuteStop(const TSharedPtr<FJsonObject>& Arguments);
	FMcpToolResult ExecutePause(const TSharedPtr<FJsonObject>& Arguments);
	FMcpToolResult ExecuteResume(const TSharedPtr<FJsonObject>& Arguments);
	FMcpToolResult ExecuteGetState(const TSharedPtr<FJsonObject>& Arguments);
	FMcpToolResult ExecuteWaitFor(const TSharedPtr<FJsonObject>& Arguments);

	FString GenerateSessionId() const;
	TSharedPtr<FJsonValue> GetActorProperty(AActor* Actor, const FString& PropertyName) const;
	UWorld* GetPIEWorld() const;
	bool CompareJsonValues(const TSharedPtr<FJsonValue>& Actual, const TSharedPtr<FJsonValue>& Expected, const FString& Operator) const;
	TSharedPtr<FJsonObject> GetWorldInfo(UWorld* PIEWorld) const;
	TArray<TSharedPtr<FJsonValue>> GetPlayersInfo(UWorld* PIEWorld) const;

	/** 注册/注销 PIE 事件回调，用于在 PIE 完成启动或停止后清除过渡标志 */
	static void RegisterPIECallbacks();
	static void UnregisterPIECallbacks();

	/** PIE 过渡状态标志：true 表示 PIE 正在启动或停止中 */
	static bool bPIETransitioning;
	
	/** PIE 事件回调的 DelegateHandle */
	static FDelegateHandle PIEStartedHandle;
	static FDelegateHandle PIEEndedHandle;
};
