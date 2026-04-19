// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Build/TriggerLiveCodingTool.h"
#include "UEBridgeMCPEditor.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "Log/McpLogCapture.h"

// Live Coding module (Windows only)
#if PLATFORM_WINDOWS
	#include "ILiveCodingModule.h"
#endif

FString UTriggerLiveCodingTool::GetToolDescription() const
{
	return TEXT("Trigger Live Coding compilation for C++ code changes. Use 'wait_for_completion' to query compilation status after triggering. Windows only.");
}

TMap<FString, FMcpSchemaProperty> UTriggerLiveCodingTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty WaitForCompletion;
	WaitForCompletion.Type = TEXT("boolean");
	WaitForCompletion.Description = TEXT("If true, query current compilation status instead of triggering a new compile (default: false, trigger mode)");
	WaitForCompletion.bRequired = false;
	Schema.Add(TEXT("wait_for_completion"), WaitForCompletion);

	return Schema;
}

FMcpToolResult UTriggerLiveCodingTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	bool bWaitForCompletion = GetBoolArgOrDefault(Arguments, TEXT("wait_for_completion"), false);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("trigger-live-coding: Triggering Live Coding compilation (query_status=%d)"),
		bWaitForCompletion);

#if !PLATFORM_WINDOWS
	return FMcpToolResult::Error(TEXT("Live Coding is only supported on Windows"));
#else

	// Try to use ILiveCodingModule for better control
	ILiveCodingModule* LiveCodingModule = FModuleManager::GetModulePtr<ILiveCodingModule>("LiveCoding");

	if (!LiveCodingModule)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("trigger-live-coding: LiveCoding module not available, falling back to console command"));

		// Fallback to console command
		if (!GEngine || !GEngine->Exec(nullptr, TEXT("LiveCoding.Compile")))
		{
			return FMcpToolResult::Error(TEXT("Failed to trigger Live Coding. Enable it in Editor Preferences > General > Live Coding."));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("status"), TEXT("triggered_async"));
		Result->SetStringField(TEXT("message"), TEXT("Live Coding triggered via console command"));
		return FMcpToolResult::Json(Result);
	}

	// Check if Live Coding is enabled
	if (!LiveCodingModule->IsEnabledForSession())
	{
		return FMcpToolResult::Error(TEXT("Live Coding is not enabled for this session. Enable it in Editor Preferences > General > Live Coding."));
	}

	if (bWaitForCompletion)
	{
		// 查询模式：检查当前编译状态（非阻塞）
		return QueryCompilationStatus(LiveCodingModule);
	}
	else
	{
		// 触发模式：fire-and-forget，立即返回
		return ExecuteAsynchronous(LiveCodingModule);
	}
#endif
}

#if PLATFORM_WINDOWS
FMcpToolResult UTriggerLiveCodingTool::QueryCompilationStatus(ILiveCodingModule* LiveCodingModule)
{
	UE_LOG(LogUEBridgeMCP, Log, TEXT("trigger-live-coding: Querying compilation status (non-blocking)..."));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("shortcut"), TEXT("Ctrl+Alt+F11"));

	// 使用非阻塞方式查询编译状态
	// Compile with None flag 会立即返回当前状态
	ELiveCodingCompileResult CompileResult;
	bool bStarted = LiveCodingModule->Compile(ELiveCodingCompileFlags::None, &CompileResult);

	FString StatusStr;
	FString MessageStr;
	bool bSuccess = false;

	switch (CompileResult)
	{
	case ELiveCodingCompileResult::Success:
		bSuccess = true;
		StatusStr = TEXT("completed");
		MessageStr = TEXT("Live Coding compilation completed successfully");
		break;

	case ELiveCodingCompileResult::NoChanges:
		bSuccess = true;
		StatusStr = TEXT("no_changes");
		MessageStr = TEXT("No code changes detected - nothing to compile");
		break;

	case ELiveCodingCompileResult::Failure:
		bSuccess = false;
		StatusStr = TEXT("failed");
		MessageStr = TEXT("Live Coding compilation failed. Check Output Log for errors.");
		break;

	case ELiveCodingCompileResult::Cancelled:
		bSuccess = false;
		StatusStr = TEXT("cancelled");
		MessageStr = TEXT("Live Coding compilation was cancelled");
		break;

	case ELiveCodingCompileResult::CompileStillActive:
		bSuccess = true;
		StatusStr = TEXT("compiling");
		MessageStr = TEXT("Compilation still in progress. Call again with wait_for_completion=true to poll status.");
		break;

	case ELiveCodingCompileResult::InProgress:
		bSuccess = true;
		StatusStr = TEXT("compiling");
		MessageStr = TEXT("Compilation in progress. Call again with wait_for_completion=true to poll status.");
		break;

	case ELiveCodingCompileResult::NotStarted:
		bSuccess = false;
		StatusStr = TEXT("not_started");
		MessageStr = TEXT("Live Coding monitor could not be started");
		break;

	default:
		bSuccess = false;
		StatusStr = TEXT("unknown");
		MessageStr = FString::Printf(TEXT("Unexpected compile result: %d"), static_cast<int32>(CompileResult));
		break;
	}

	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("status"), StatusStr);
	Result->SetStringField(TEXT("message"), MessageStr);

	// 查询最近的编译错误日志
	TArray<FMcpLogEntry> RecentErrors = FMcpLogCapture::Get().GetLogs(
		TEXT("LogLiveCoding"),
		ELogVerbosity::Error,
		10,
		TEXT("")
	);

	if (RecentErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FMcpLogEntry& Entry : RecentErrors)
		{
			ErrorArray.Add(MakeShareable(new FJsonValueString(Entry.Message)));
		}
		Result->SetArrayField(TEXT("recent_errors"), ErrorArray);
	}

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UTriggerLiveCodingTool::ExecuteAsynchronous(ILiveCodingModule* LiveCodingModule)
{
	// Just trigger compilation and return immediately
	LiveCodingModule->Compile(ELiveCodingCompileFlags::None, nullptr);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("triggered_async"));
	Result->SetStringField(TEXT("message"), TEXT("Live Coding compilation initiated. Check Output Log for results."));
	Result->SetStringField(TEXT("shortcut"), TEXT("Ctrl+Alt+F11"));

	UE_LOG(LogUEBridgeMCP, Log, TEXT("trigger-live-coding: Async compilation triggered"));

	return FMcpToolResult::Json(Result);
}
#endif