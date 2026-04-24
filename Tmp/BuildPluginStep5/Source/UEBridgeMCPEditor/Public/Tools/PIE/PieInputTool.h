// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "PieInputTool.generated.h"

/**
 * Consolidated PIE input simulation tool.
 * Actions: key, action, axis, move-to, look-at
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UPieInputTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("pie-input"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("action")}; }
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	FMcpToolResult ExecuteKey(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FMcpToolResult ExecuteAction(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FMcpToolResult ExecuteAxis(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FMcpToolResult ExecuteMoveTo(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);
	FMcpToolResult ExecuteLookAt(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld);

	UWorld* GetPIEWorld() const;
	APlayerController* GetPlayerController(UWorld* PIEWorld, int32 PlayerIndex) const;
};
