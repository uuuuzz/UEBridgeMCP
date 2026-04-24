// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "Log/McpLogCapture.h"
#include "RunPythonScriptTool.generated.h"

/**
 * Execute Python scripts in Unreal Editor's Python environment.
 * Requires PythonScriptPlugin to be enabled.
 * Supports inline scripts or script files with optional arguments.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API URunPythonScriptTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("run-python-script"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; } // Either script or script_path required
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }
	virtual FString GetToolKind() const override { return TEXT("mutation"); }
	virtual FString GetResourceScope() const override { return TEXT("editor"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }

private:
	// Execute a Python command and capture output
	FString ExecutePython(const FString& Command, bool& bOutSuccess, FString& OutError);

	// Read script file from disk
	bool ReadScriptFile(const FString& ScriptPath, FString& OutScript, FString& OutError);

	// Build Python command with arguments and additional Python paths
	FString BuildPythonCommand(const FString& Script, const TSharedPtr<FJsonObject>& Arguments, const TArray<FString>& PythonPaths);
};
