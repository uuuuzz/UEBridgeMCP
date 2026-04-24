// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "McpToolResult.generated.h"

/**
 * Content type for tool results
 */
UENUM()
enum class EMcpContentType : uint8
{
	Text,
	Image,
	Resource
};

/**
 * Tool execution result
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpToolResult
{
	GENERATED_BODY()

	/** Whether execution succeeded */
	UPROPERTY()
	bool bSuccess = false;

	/** Result content (array of content items) */
	TArray<TSharedPtr<FJsonObject>> Content;

	/** Is this an error result */
	UPROPERTY()
	bool bIsError = false;

	/** Create success result with text content */
	static FMcpToolResult Text(const FString& InText);

	/** Create success result with multiple text items */
	static FMcpToolResult TextArray(const TArray<FString>& InTexts);

	/** Create success result with JSON content */
	static FMcpToolResult Json(TSharedPtr<FJsonObject> JsonContent);

	/** Create success result with JSON content as formatted text */
	static FMcpToolResult JsonAsText(TSharedPtr<FJsonObject> JsonContent);

	/** Create error result */
	static FMcpToolResult Error(const FString& Message);

	/** Create structured JSON result with optional error flag */
	static FMcpToolResult StructuredJson(TSharedPtr<FJsonObject> Payload, bool bInIsError = false);

	/** Create structured success result */
	static FMcpToolResult StructuredSuccess(
		TSharedPtr<FJsonObject> Payload,
		const FString& SummaryText = TEXT(""),
		TSharedPtr<FJsonObject> Diagnostics = nullptr,
		TSharedPtr<FJsonObject> Stats = nullptr);

	/**
	 * Create structured error result with error code, message, and optional details.
	 * Emits a short text summary plus standard structured content.
	 */
	static FMcpToolResult StructuredError(
		const FString& Code,
		const FString& Message,
		TSharedPtr<FJsonObject> Details = nullptr,
		TSharedPtr<FJsonObject> PartialResult = nullptr);

	/** Convert to JSON for tools/call response */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Access the standard structured content payload. */
	TSharedPtr<FJsonObject> GetStructuredContent() const { return StructuredContentPayload; }

	/** Access optional diagnostics metadata payload. */
	TSharedPtr<FJsonObject> GetDiagnostics() const { return DiagnosticsPayload; }

	/** Access optional timing metadata payload. */
	TSharedPtr<FJsonObject> GetTiming() const { return TimingPayload; }

	/** Access optional stats metadata payload. */
	TSharedPtr<FJsonObject> GetStats() const { return StatsPayload; }

	/** Attach timing metadata */
	void SetTimingMs(double InElapsedMs);

	/** Attach diagnostics metadata */
	void SetDiagnostics(TSharedPtr<FJsonObject> InDiagnostics);

	/** Attach stats metadata */
	void SetStats(TSharedPtr<FJsonObject> InStats);

	/** Attach a short text summary for human-readable clients */
	void SetSummaryText(const FString& InSummaryText);

private:
	/** Optional standard MCP structured content payload */
	TSharedPtr<FJsonObject> StructuredContentPayload;

	/** Optional diagnostics payload stored in _meta */
	TSharedPtr<FJsonObject> DiagnosticsPayload;

	/** Optional timing payload stored in _meta */
	TSharedPtr<FJsonObject> TimingPayload;

	/** Optional stats payload stored in _meta */
	TSharedPtr<FJsonObject> StatsPayload;
};
