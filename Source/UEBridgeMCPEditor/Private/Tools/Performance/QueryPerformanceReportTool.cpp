// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Performance/QueryPerformanceReportTool.h"

#include "Tools/Performance/PerformanceToolUtils.h"

FString UQueryPerformanceReportTool::GetToolDescription() const
{
	return TEXT("Return a lightweight performance summary for the editor or PIE world, including frame timing, actor/object counts, and memory overview.");
}

TMap<FString, FMcpSchemaProperty> UQueryPerformanceReportTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to inspect"), { TEXT("editor"), TEXT("pie") }));
	return Schema;
}

FMcpToolResult UQueryPerformanceReportTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	UWorld* World = PerformanceToolUtils::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("Requested world is not available"));
	}

	return FMcpToolResult::StructuredSuccess(
		PerformanceToolUtils::BuildPerformanceReport(World, RequestedWorldType),
		TEXT("Performance report ready"));
}
