// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CapturePerformanceSnapshotTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UCapturePerformanceSnapshotTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("capture-performance-snapshot"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("utility"); }
	virtual FString GetResourceScope() const override { return TEXT("performance"); }
	virtual bool MutatesState() const override { return false; }
};
