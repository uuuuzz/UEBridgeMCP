// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryLandscapeSummaryTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryLandscapeSummaryTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-landscape-summary"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("query"); }
	virtual bool RequiresGameThread() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
