// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "RunProjectMaintenanceChecksTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API URunProjectMaintenanceChecksTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("run-project-maintenance-checks"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("query"); }
	virtual FString GetResourceScope() const override { return TEXT("project"); }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool RequiresGameThread() const override { return true; }
};
