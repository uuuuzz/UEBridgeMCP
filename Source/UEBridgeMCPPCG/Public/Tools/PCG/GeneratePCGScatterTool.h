// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "GeneratePCGScatterTool.generated.h"

UCLASS()
class UEBRIDGEMCPPCG_API UGeneratePCGScatterTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("generate-pcg-scatter"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("generate"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
