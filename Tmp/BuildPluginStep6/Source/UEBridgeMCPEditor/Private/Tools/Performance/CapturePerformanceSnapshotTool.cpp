// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Performance/CapturePerformanceSnapshotTool.h"

#include "Tools/Debug/CaptureViewportTool.h"
#include "Tools/Debug/GetLogsTool.h"
#include "Tools/Performance/PerformanceToolUtils.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UCapturePerformanceSnapshotTool::GetToolDescription() const
{
	return TEXT("Capture a timestamped performance snapshot under Saved/UEBridgeMCP/PerformanceSnapshots, including JSON report data and optional viewport/log artifacts.");
}

TMap<FString, FMcpSchemaProperty> UCapturePerformanceSnapshotTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to inspect"), { TEXT("editor"), TEXT("pie") }));
	Schema.Add(TEXT("include_viewport"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Capture the active editor viewport as part of the snapshot")));
	Schema.Add(TEXT("include_logs"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Capture a warning-level log snapshot")));
	return Schema;
}

FMcpToolResult UCapturePerformanceSnapshotTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bIncludeViewport = GetBoolArgOrDefault(Arguments, TEXT("include_viewport"), true);
	const bool bIncludeLogs = GetBoolArgOrDefault(Arguments, TEXT("include_logs"), true);

	UWorld* World = PerformanceToolUtils::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("Requested world is not available"));
	}

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString SnapshotDirectory = FPaths::ProjectSavedDir() / TEXT("UEBridgeMCP/PerformanceSnapshots") / Timestamp;
	if (!IFileManager::Get().MakeDirectory(*SnapshotDirectory, true) && !IFileManager::Get().DirectoryExists(*SnapshotDirectory))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_FILE_WRITE_FAILED"), TEXT("Failed to create performance snapshot directory"));
	}

	TSharedPtr<FJsonObject> Report = PerformanceToolUtils::BuildPerformanceReport(World, RequestedWorldType);
	Report->SetStringField(TEXT("snapshot_directory"), SnapshotDirectory);
	Report->SetStringField(TEXT("captured_at"), FDateTime::Now().ToIso8601());

	if (bIncludeViewport)
	{
		TSharedPtr<FJsonObject> CaptureArgs = MakeShareable(new FJsonObject);
		CaptureArgs->SetStringField(TEXT("output_path"), SnapshotDirectory / TEXT("viewport.png"));
		CaptureArgs->SetStringField(TEXT("format"), TEXT("png"));

		FMcpToolResult CaptureResult = GetMutableDefault<UCaptureViewportTool>()->Execute(CaptureArgs, Context);
		Report->SetObjectField(TEXT("viewport_capture"), CaptureResult.ToJson());
	}

	if (bIncludeLogs)
	{
		TSharedPtr<FJsonObject> LogArgs = MakeShareable(new FJsonObject);
		LogArgs->SetStringField(TEXT("severity"), TEXT("Warning"));
		LogArgs->SetNumberField(TEXT("limit"), 25);

		FMcpToolResult LogResult = GetMutableDefault<UGetLogsTool>()->Execute(LogArgs, Context);
		Report->SetObjectField(TEXT("log_snapshot"), LogResult.ToJson());
	}

	const FString ReportPath = SnapshotDirectory / TEXT("report.json");
	FString ReportString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportString);
	if (!FJsonSerializer::Serialize(Report.ToSharedRef(), Writer) || !FFileHelper::SaveStringToFile(ReportString, *ReportPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_FILE_WRITE_FAILED"), TEXT("Failed to write performance report JSON"));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("snapshot_directory"), SnapshotDirectory);
	Response->SetStringField(TEXT("report_path"), ReportPath);
	Response->SetObjectField(TEXT("report"), Report);
	return FMcpToolResult::StructuredJson(Response);
}
