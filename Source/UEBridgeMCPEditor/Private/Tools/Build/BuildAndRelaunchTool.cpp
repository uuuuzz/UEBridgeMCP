// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Build/BuildAndRelaunchTool.h"
#include "UEBridgeMCPEditor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Editor.h"

FString UBuildAndRelaunchTool::GetToolDescription() const
{
	return TEXT("Close THIS editor instance (identified by PID), trigger a full project build, and relaunch the editor. Only affects the MCP-connected editor instance, not other running editors.");
}

TMap<FString, FMcpSchemaProperty> UBuildAndRelaunchTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty BuildConfig;
	BuildConfig.Type = TEXT("string");
	BuildConfig.Description = TEXT("Build configuration: Development, Debug, or Shipping (default: Development)");
	BuildConfig.bRequired = false;
	Schema.Add(TEXT("build_config"), BuildConfig);

	FMcpSchemaProperty SkipRelaunch;
	SkipRelaunch.Type = TEXT("boolean");
	SkipRelaunch.Description = TEXT("Skip relaunching the editor after build (default: false)");
	SkipRelaunch.bRequired = false;
	Schema.Add(TEXT("skip_relaunch"), SkipRelaunch);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate paths and report the planned build/relaunch without closing the editor")));
	Schema.Add(TEXT("confirm_shutdown"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Required for non-dry-run execution because this closes the connected editor instance")));

	return Schema;
}

FMcpToolResult UBuildAndRelaunchTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	(void)Context;
#if PLATFORM_WINDOWS
	FString BuildConfig = GetStringArgOrDefault(Arguments, TEXT("build_config"), TEXT("Development"));
	bool bSkipRelaunch = GetBoolArgOrDefault(Arguments, TEXT("skip_relaunch"), false);
	bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	bool bConfirmShutdown = GetBoolArgOrDefault(Arguments, TEXT("confirm_shutdown"), false);

	// Validate build configuration
	if (BuildConfig != TEXT("Development") && BuildConfig != TEXT("Debug") && BuildConfig != TEXT("Shipping"))
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Invalid build configuration: %s. Must be Development, Debug, or Shipping."), *BuildConfig));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("build-and-relaunch: Starting build and relaunch workflow (Config: %s, SkipRelaunch: %s)"),
		*BuildConfig, bSkipRelaunch ? TEXT("true") : TEXT("false"));

	// Get project paths
	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("Could not determine project path"));
	}

	FString ProjectName = FPaths::GetBaseFilename(ProjectPath);
	FString ProjectDir = FPaths::GetPath(ProjectPath);

	// Get engine paths
	FString EngineDir = FPaths::EngineDir();
	FString BuildBatchFile = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat"));
	FString EditorExecutable = FPaths::Combine(EngineDir, TEXT("Binaries/Win64/UnrealEditor.exe"));

	if (!FPaths::FileExists(BuildBatchFile))
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Build script not found: %s"), *BuildBatchFile));
	}

	if (!bSkipRelaunch && !FPaths::FileExists(EditorExecutable))
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Editor executable not found: %s"), *EditorExecutable));
	}

	TSharedPtr<FJsonObject> Plan = MakeShareable(new FJsonObject);
	Plan->SetStringField(TEXT("tool"), GetToolName());
	Plan->SetBoolField(TEXT("dry_run"), bDryRun);
	Plan->SetStringField(TEXT("project"), *ProjectName);
	Plan->SetStringField(TEXT("project_path"), ProjectPath);
	Plan->SetStringField(TEXT("build_config"), *BuildConfig);
	Plan->SetBoolField(TEXT("will_relaunch"), !bSkipRelaunch);
	Plan->SetBoolField(TEXT("will_shutdown_editor"), !bDryRun);
	Plan->SetBoolField(TEXT("would_shutdown_editor"), true);
	Plan->SetStringField(TEXT("build_script"), BuildBatchFile);
	Plan->SetStringField(TEXT("editor_executable"), EditorExecutable);

	if (bDryRun)
	{
		Plan->SetBoolField(TEXT("success"), true);
		Plan->SetBoolField(TEXT("would_execute"), true);
		return FMcpToolResult::StructuredJson(Plan);
	}

	if (!bConfirmShutdown)
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_CONFIRMATION_REQUIRED"),
			TEXT("build-and-relaunch requires confirm_shutdown=true because it closes the MCP-connected editor instance"),
			Plan);
	}

	// Create a batch script to handle the workflow
	FString TempScriptPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp"), TEXT("BuildAndRelaunch.bat"));
	FString TempScriptDir = FPaths::GetPath(TempScriptPath);

	// Ensure temp directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TempScriptDir))
	{
		if (!PlatformFile.CreateDirectoryTree(*TempScriptDir))
		{
			return FMcpToolResult::Error(FString::Printf(TEXT("Failed to create temp directory: %s"), *TempScriptDir));
		}
	}

	// Get current process ID to wait for specifically this instance
	uint32 CurrentPID = FPlatformProcess::GetCurrentProcessId();

	// Build the batch script
	FString BatchScript = TEXT("@echo off\n");
	BatchScript += FString::Printf(TEXT("echo Waiting for Unreal Editor (PID: %d) to close...\n"), CurrentPID);
	BatchScript += TEXT("\n");

	// Wait for this specific process to exit (not just any editor)
	BatchScript += TEXT(":WAIT_LOOP\n");
	BatchScript += FString::Printf(TEXT("tasklist /FI \"PID eq %d\" 2>NUL | find \"%d\" >NUL\n"), CurrentPID, CurrentPID);
	BatchScript += TEXT("if %ERRORLEVEL% EQU 0 (\n");
	BatchScript += TEXT("    timeout /t 1 /nobreak >nul\n");
	BatchScript += TEXT("    goto WAIT_LOOP\n");
	BatchScript += TEXT(")\n");
	BatchScript += TEXT("echo Editor closed.\n");
	BatchScript += TEXT("\n");

	// Build command
	BatchScript += FString::Printf(TEXT("echo Building %s (%s)...\n"), *ProjectName, *BuildConfig);
	BatchScript += FString::Printf(TEXT("call \"%s\" %sEditor Win64 %s \"%s\" -waitmutex\n"),
		*BuildBatchFile,
		*ProjectName,
		*BuildConfig,
		*ProjectPath);
	BatchScript += TEXT("\n");
	BatchScript += TEXT("if %ERRORLEVEL% NEQ 0 (\n");
	BatchScript += TEXT("    echo Build failed with error code %ERRORLEVEL%\n");
	BatchScript += TEXT("    pause\n");
	BatchScript += TEXT("    exit /b %ERRORLEVEL%\n");
	BatchScript += TEXT(")\n");
	BatchScript += TEXT("\n");

	// Relaunch command (if not skipped)
	if (!bSkipRelaunch)
	{
		BatchScript += TEXT("echo Build completed successfully. Relaunching editor...\n");
		BatchScript += TEXT("timeout /t 2 /nobreak >nul\n");
		BatchScript += FString::Printf(TEXT("start \"\" \"%s\" \"%s\"\n"), *EditorExecutable, *ProjectPath);
	}
	else
	{
		BatchScript += TEXT("echo Build completed successfully.\n");
	}

	BatchScript += TEXT("\n");
	BatchScript += FString::Printf(TEXT("del \"%s\"\n"), *TempScriptPath);

	// Write the batch script
	if (!FFileHelper::SaveStringToFile(BatchScript, *TempScriptPath))
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Failed to create batch script: %s"), *TempScriptPath));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("build-and-relaunch: Created batch script at: %s"), *TempScriptPath);

	// Launch the batch script via cmd.exe (batch files can't be executed directly by CreateProc)
	FString CmdArgs = FString::Printf(TEXT("/c \"%s\""), *TempScriptPath);
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		TEXT("cmd.exe"),
		*CmdArgs,
		true,  // bLaunchDetached
		false, // bLaunchHidden
		false, // bLaunchReallyHidden
		nullptr,
		0,     // PriorityModifier
		nullptr,
		nullptr
	);

	if (!ProcHandle.IsValid())
	{
		return FMcpToolResult::Error(TEXT("Failed to launch build script"));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("build-and-relaunch: Batch script launched successfully (PID: %d). Requesting editor shutdown..."), CurrentPID);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("initiated"));
	Result->SetStringField(TEXT("project"), *ProjectName);
	Result->SetStringField(TEXT("build_config"), *BuildConfig);
	Result->SetBoolField(TEXT("will_relaunch"), !bSkipRelaunch);
	Result->SetNumberField(TEXT("editor_pid"), CurrentPID);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Build and relaunch workflow initiated for this editor instance (PID: %d). Editor will close momentarily."), CurrentPID));

	// Request editor shutdown
	// Use a small delay to allow the response to be sent
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime) -> bool
	{
		FPlatformMisc::RequestExit(false);
		return false; // Don't repeat
	}), 1.0f);

	return FMcpToolResult::Json(Result);
#else
	return FMcpToolResult::Error(TEXT("build-and-relaunch is only supported on Windows"));
#endif
}
