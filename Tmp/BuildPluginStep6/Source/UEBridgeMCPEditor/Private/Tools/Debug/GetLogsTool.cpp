// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Debug/GetLogsTool.h"
#include "Log/McpLogCapture.h"
#include "UEBridgeMCPEditor.h"

TMap<FString, FMcpSchemaProperty> UGetLogsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty Category;
	Category.Type = TEXT("string");
	Category.Description = TEXT("Filter by log category (e.g., 'LogUEBridgeMCP', 'LogTemp', or '*' for all). Supports wildcards.");
	Category.bRequired = false;
	Schema.Add(TEXT("category"), Category);

	FMcpSchemaProperty Severity;
	Severity.Type = TEXT("string");
	Severity.Description = TEXT("Minimum severity level: 'Verbose', 'Log', 'Warning', 'Error' (default: 'Log')");
	Severity.bRequired = false;
	Schema.Add(TEXT("severity"), Severity);

	FMcpSchemaProperty Limit;
	Limit.Type = TEXT("integer");
	Limit.Description = TEXT("Maximum number of entries to return (default: 100, max: 1000)");
	Limit.bRequired = false;
	Schema.Add(TEXT("limit"), Limit);

	FMcpSchemaProperty Search;
	Search.Type = TEXT("string");
	Search.Description = TEXT("Filter messages containing this substring (case-insensitive)");
	Search.bRequired = false;
	Schema.Add(TEXT("search"), Search);

	return Schema;
}

FMcpToolResult UGetLogsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString CategoryFilter = GetStringArgOrDefault(Arguments, TEXT("category"), TEXT("*"));
	FString SeverityStr = GetStringArgOrDefault(Arguments, TEXT("severity"), TEXT("Log"));
	int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 100), 1, 1000);
	FString SearchFilter = GetStringArgOrDefault(Arguments, TEXT("search"), TEXT(""));

	UE_LOG(LogUEBridgeMCP, Log, TEXT("get-logs: category='%s', severity='%s', limit=%d, search='%s'"),
		*CategoryFilter, *SeverityStr, Limit, *SearchFilter);

	ELogVerbosity::Type MinVerbosity = ParseVerbosity(SeverityStr);

	// Query logs from capture buffer
	TArray<FMcpLogEntry> Entries = FMcpLogCapture::Get().GetLogs(
		CategoryFilter, MinVerbosity, Limit, SearchFilter);

	// Build result JSON
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> EntriesArray;

	for (const FMcpLogEntry& Entry : Entries)
	{
		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
		EntryObj->SetStringField(TEXT("timestamp"), Entry.Timestamp.ToIso8601());
		EntryObj->SetStringField(TEXT("category"), Entry.Category.ToString());
		EntryObj->SetStringField(TEXT("severity"), VerbosityToString(Entry.Verbosity));
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		EntriesArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
	}

	Result->SetArrayField(TEXT("entries"), EntriesArray);
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetNumberField(TEXT("total_captured"), FMcpLogCapture::Get().GetTotalCaptured());
	Result->SetNumberField(TEXT("buffer_capacity"), FMcpLogCapture::Get().GetBufferCapacity());

	// Add applied filters for context
	TSharedPtr<FJsonObject> Filters = MakeShareable(new FJsonObject);
	Filters->SetStringField(TEXT("category"), CategoryFilter);
	Filters->SetStringField(TEXT("severity"), SeverityStr);
	Filters->SetNumberField(TEXT("limit"), Limit);
	if (!SearchFilter.IsEmpty())
	{
		Filters->SetStringField(TEXT("search"), SearchFilter);
	}
	Result->SetObjectField(TEXT("filters"), Filters);

	return FMcpToolResult::Json(Result);
}

ELogVerbosity::Type UGetLogsTool::ParseVerbosity(const FString& VerbosityStr)
{
	if (VerbosityStr.Equals(TEXT("Verbose"), ESearchCase::IgnoreCase) ||
		VerbosityStr.Equals(TEXT("VeryVerbose"), ESearchCase::IgnoreCase))
	{
		return ELogVerbosity::Verbose;
	}
	else if (VerbosityStr.Equals(TEXT("Log"), ESearchCase::IgnoreCase))
	{
		return ELogVerbosity::Log;
	}
	else if (VerbosityStr.Equals(TEXT("Display"), ESearchCase::IgnoreCase))
	{
		return ELogVerbosity::Display;
	}
	else if (VerbosityStr.Equals(TEXT("Warning"), ESearchCase::IgnoreCase))
	{
		return ELogVerbosity::Warning;
	}
	else if (VerbosityStr.Equals(TEXT("Error"), ESearchCase::IgnoreCase))
	{
		return ELogVerbosity::Error;
	}
	else if (VerbosityStr.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase))
	{
		return ELogVerbosity::Fatal;
	}

	// Default to Log
	return ELogVerbosity::Log;
}

FString UGetLogsTool::VerbosityToString(ELogVerbosity::Type Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal:
		return TEXT("Fatal");
	case ELogVerbosity::Error:
		return TEXT("Error");
	case ELogVerbosity::Warning:
		return TEXT("Warning");
	case ELogVerbosity::Display:
		return TEXT("Display");
	case ELogVerbosity::Log:
		return TEXT("Log");
	case ELogVerbosity::Verbose:
		return TEXT("Verbose");
	case ELogVerbosity::VeryVerbose:
		return TEXT("VeryVerbose");
	default:
		return TEXT("Unknown");
	}
}
