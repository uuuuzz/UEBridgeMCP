// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "PerformanceDetailTools.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryRenderStatsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-render-stats"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetResourceScope() const override { return TEXT("performance"); }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryMemoryReportTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-memory-report"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetResourceScope() const override { return TEXT("performance"); }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};

UCLASS()
class UEBRIDGEMCPEDITOR_API UProfileVisibleActorsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("profile-visible-actors"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetResourceScope() const override { return TEXT("performance"); }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
