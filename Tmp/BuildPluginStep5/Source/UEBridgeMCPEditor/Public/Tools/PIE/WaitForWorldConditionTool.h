// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "WaitForWorldConditionTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UWaitForWorldConditionTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("wait-for-world-condition"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	bool EvaluateCondition(UWorld* World, const TSharedPtr<FJsonObject>& Condition, TSharedPtr<FJsonObject>& OutObservation) const;
};
