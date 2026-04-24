// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/McpToolResult.h"

namespace
{
	const TCHAR* DefaultSuccessSummary = TEXT("Request succeeded");
	const TCHAR* DefaultErrorSummary = TEXT("Request failed");

	TSharedPtr<FJsonObject> BuildMetaPayload(
		const TSharedPtr<FJsonObject>& Diagnostics,
		const TSharedPtr<FJsonObject>& Timing,
		const TSharedPtr<FJsonObject>& Stats)
	{
		if (!Diagnostics.IsValid() && !Timing.IsValid() && !Stats.IsValid())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> MetaObject = MakeShareable(new FJsonObject);
		if (Diagnostics.IsValid())
		{
			MetaObject->SetObjectField(TEXT("diagnostics"), Diagnostics);
		}
		if (Timing.IsValid())
		{
			MetaObject->SetObjectField(TEXT("timing"), Timing);
		}
		if (Stats.IsValid())
		{
			MetaObject->SetObjectField(TEXT("stats"), Stats);
		}
		return MetaObject;
	}
}

FMcpToolResult FMcpToolResult::Text(const FString& InText)
{
	FMcpToolResult Result;
	Result.bSuccess = true;
	Result.bIsError = false;

	TSharedPtr<FJsonObject> ContentItem = MakeShareable(new FJsonObject);
	ContentItem->SetStringField(TEXT("type"), TEXT("text"));
	ContentItem->SetStringField(TEXT("text"), InText);
	Result.Content.Add(ContentItem);

	Result.StructuredContentPayload = MakeShareable(new FJsonObject);
	Result.StructuredContentPayload->SetStringField(TEXT("message"), InText);
	return Result;
}

FMcpToolResult FMcpToolResult::TextArray(const TArray<FString>& InTexts)
{
	FMcpToolResult Result;
	Result.bSuccess = true;
	Result.bIsError = false;

	for (const FString& Text : InTexts)
	{
		TSharedPtr<FJsonObject> ContentItem = MakeShareable(new FJsonObject);
		ContentItem->SetStringField(TEXT("type"), TEXT("text"));
		ContentItem->SetStringField(TEXT("text"), Text);
		Result.Content.Add(ContentItem);
	}

	Result.StructuredContentPayload = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FString& Text : InTexts)
	{
		MessagesArray.Add(MakeShareable(new FJsonValueString(Text)));
	}
	Result.StructuredContentPayload->SetArrayField(TEXT("messages"), MessagesArray);
	return Result;
}

FMcpToolResult FMcpToolResult::Json(TSharedPtr<FJsonObject> JsonContent)
{
	FMcpToolResult Result;
	Result.bSuccess = true;
	Result.bIsError = false;
	Result.StructuredContentPayload = JsonContent;
	Result.SetSummaryText(TEXT("Structured result ready"));
	return Result;
}

FMcpToolResult FMcpToolResult::JsonAsText(TSharedPtr<FJsonObject> JsonContent)
{
	return Json(JsonContent);
}

FMcpToolResult FMcpToolResult::Error(const FString& Message)
{
	FMcpToolResult Result;
	Result.bSuccess = false;
	Result.bIsError = true;

	TSharedPtr<FJsonObject> ContentItem = MakeShareable(new FJsonObject);
	ContentItem->SetStringField(TEXT("type"), TEXT("text"));
	ContentItem->SetStringField(TEXT("text"), Message);
	Result.Content.Add(ContentItem);

	Result.StructuredContentPayload = MakeShareable(new FJsonObject);
	Result.StructuredContentPayload->SetStringField(TEXT("code"), TEXT("UEBMCP_ERROR"));
	Result.StructuredContentPayload->SetStringField(TEXT("message"), Message);
	return Result;
}

FMcpToolResult FMcpToolResult::StructuredJson(TSharedPtr<FJsonObject> Payload, bool bInIsError)
{
	FMcpToolResult Result;
	Result.bSuccess = !bInIsError;
	Result.bIsError = bInIsError;
	Result.StructuredContentPayload = Payload;
	return Result;
}

FMcpToolResult FMcpToolResult::StructuredSuccess(
	TSharedPtr<FJsonObject> Payload,
	const FString& SummaryText,
	TSharedPtr<FJsonObject> Diagnostics,
	TSharedPtr<FJsonObject> Stats)
{
	FMcpToolResult Result = StructuredJson(Payload, false);
	Result.DiagnosticsPayload = Diagnostics;
	Result.StatsPayload = Stats;
	if (!SummaryText.IsEmpty())
	{
		Result.SetSummaryText(SummaryText);
	}
	return Result;
}

FMcpToolResult FMcpToolResult::StructuredError(
	const FString& Code,
	const FString& Message,
	TSharedPtr<FJsonObject> Details,
	TSharedPtr<FJsonObject> PartialResult)
{
	FMcpToolResult Result;
	Result.bSuccess = false;
	Result.bIsError = true;

	TSharedPtr<FJsonObject> ErrorObject = MakeShareable(new FJsonObject);
	ErrorObject->SetStringField(TEXT("code"), Code);
	ErrorObject->SetStringField(TEXT("message"), Message);
	if (Details.IsValid())
	{
		ErrorObject->SetObjectField(TEXT("details"), Details);
	}
	if (PartialResult.IsValid())
	{
		ErrorObject->SetObjectField(TEXT("partial_result"), PartialResult);
	}
	Result.StructuredContentPayload = ErrorObject;

	const FString TextMessage = FString::Printf(TEXT("[%s] %s"), *Code, *Message);
	Result.SetSummaryText(TextMessage);
	return Result;
}

TSharedPtr<FJsonObject> FMcpToolResult::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	TArray<TSharedPtr<FJsonValue>> ContentArray;
	if (Content.Num() == 0)
	{
		TSharedPtr<FJsonObject> FallbackContent = MakeShareable(new FJsonObject);
		FallbackContent->SetStringField(TEXT("type"), TEXT("text"));
		FallbackContent->SetStringField(TEXT("text"), bIsError ? DefaultErrorSummary : DefaultSuccessSummary);
		ContentArray.Add(MakeShareable(new FJsonValueObject(FallbackContent)));
	}
	else
	{
		for (const TSharedPtr<FJsonObject>& ContentItem : Content)
		{
			ContentArray.Add(MakeShareable(new FJsonValueObject(ContentItem)));
		}
	}
	JsonObject->SetArrayField(TEXT("content"), ContentArray);

	if (StructuredContentPayload.IsValid())
	{
		JsonObject->SetObjectField(TEXT("structuredContent"), StructuredContentPayload);
	}

	if (bIsError)
	{
		JsonObject->SetBoolField(TEXT("isError"), true);
	}

	if (TSharedPtr<FJsonObject> MetaPayload = BuildMetaPayload(DiagnosticsPayload, TimingPayload, StatsPayload))
	{
		JsonObject->SetObjectField(TEXT("_meta"), MetaPayload);
	}

	return JsonObject;
}

void FMcpToolResult::SetTimingMs(double InElapsedMs)
{
	TimingPayload = MakeShareable(new FJsonObject);
	TimingPayload->SetNumberField(TEXT("duration_ms"), InElapsedMs);
}

void FMcpToolResult::SetDiagnostics(TSharedPtr<FJsonObject> InDiagnostics)
{
	DiagnosticsPayload = InDiagnostics;
}

void FMcpToolResult::SetStats(TSharedPtr<FJsonObject> InStats)
{
	StatsPayload = InStats;
}

void FMcpToolResult::SetSummaryText(const FString& InSummaryText)
{
	if (InSummaryText.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> ContentItem = MakeShareable(new FJsonObject);
	ContentItem->SetStringField(TEXT("type"), TEXT("text"));
	ContentItem->SetStringField(TEXT("text"), InSummaryText);
	Content.Add(ContentItem);
}
