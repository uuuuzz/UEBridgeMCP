// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "BuildAndRelaunchTool.generated.h"

/**
 * Close THIS editor instance, trigger a full project build, and relaunch the editor.
 * This tool handles the complete workflow for rebuilding the project.
 * Uses process ID (PID) to ensure only the MCP-connected editor instance is affected.
 * Other running editor instances are not affected.
 * Windows only.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UBuildAndRelaunchTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("build-and-relaunch"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }
	virtual FString GetToolKind() const override { return TEXT("mutation"); }
	virtual FString GetResourceScope() const override { return TEXT("build"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
};
