// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "ExternalAI/OpenAICompatibleExternalAIProvider.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString NormalizeBaseUrl(const FString& InBaseUrl)
	{
		FString Result = InBaseUrl;
		Result.TrimStartAndEndInline();
		while (Result.EndsWith(TEXT("/")))
		{
			Result.LeftChopInline(1);
		}
		return Result;
	}

	bool SerializeJsonObject(const TSharedPtr<FJsonObject>& Object, FString& OutString)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
		return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	}

	FString BuildUserPrompt(const FExternalAIRequest& Request)
	{
		FString Prompt = FString::Printf(TEXT("Content brief:\n%s"), *Request.Brief);

		if (!Request.ReferencePrompt.IsEmpty())
		{
			Prompt += FString::Printf(TEXT("\n\nReference prompt:\n%s"), *Request.ReferencePrompt);
		}

		if (Request.Metadata.IsValid())
		{
			FString MetadataString;
			if (SerializeJsonObject(Request.Metadata, MetadataString))
			{
				Prompt += FString::Printf(TEXT("\n\nStructured metadata:\n%s"), *MetadataString);
			}
		}

		if (Request.ResponseFormat.Equals(TEXT("json"), ESearchCase::IgnoreCase))
		{
			Prompt += TEXT("\n\nReturn a single JSON object only.");
		}
		else
		{
			Prompt += TEXT("\n\nReturn concise, production-ready text.");
		}

		return Prompt;
	}

	FString ExtractContentString(const TSharedPtr<FJsonObject>& ChoiceObject)
	{
		if (!ChoiceObject.IsValid())
		{
			return FString();
		}

		const TSharedPtr<FJsonObject>* MessageObject = nullptr;
		if (!ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject) || !MessageObject || !(*MessageObject).IsValid())
		{
			return FString();
		}

		FString Content;
		if ((*MessageObject)->TryGetStringField(TEXT("content"), Content))
		{
			return Content;
		}

		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if ((*MessageObject)->TryGetArrayField(TEXT("content"), Parts) && Parts)
		{
			TArray<FString> TextParts;
			for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
			{
				const TSharedPtr<FJsonObject>* PartObject = nullptr;
				if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || !PartObject || !(*PartObject).IsValid())
				{
					continue;
				}

				FString PartText;
				if ((*PartObject)->TryGetStringField(TEXT("text"), PartText) && !PartText.IsEmpty())
				{
					TextParts.Add(PartText);
				}
			}

			return FString::Join(TextParts, TEXT("\n"));
		}

		return FString();
	}
}

bool FOpenAICompatibleExternalAIProvider::IsConfigured(const FExternalAIRequest& Request, FString& OutReason) const
{
	if (Request.ApiBaseUrl.IsEmpty())
	{
		OutReason = TEXT("API base URL is not configured");
		return false;
	}

	if (Request.ApiKey.IsEmpty())
	{
		OutReason = TEXT("API key is not configured");
		return false;
	}

	if (Request.Model.IsEmpty())
	{
		OutReason = TEXT("Model is not configured");
		return false;
	}

	return true;
}

bool FOpenAICompatibleExternalAIProvider::Generate(
	const FExternalAIRequest& Request,
	FExternalAIResponse& OutResponse,
	FString& OutErrorCode,
	FString& OutErrorMessage)
{
	FString ConfigurationError;
	if (!IsConfigured(Request, ConfigurationError))
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_NOT_CONFIGURED");
		OutErrorMessage = ConfigurationError;
		return false;
	}

	const FString Url = NormalizeBaseUrl(Request.ApiBaseUrl) + TEXT("/chat/completions");
	const FString SystemPrompt = Request.ResponseFormat.Equals(TEXT("json"), ESearchCase::IgnoreCase)
		? TEXT("You generate structured JSON-only creative planning output for Unreal Engine editor workflows.")
		: TEXT("You generate concise creative planning output for Unreal Engine editor workflows.");
	const FString UserPrompt = BuildUserPrompt(Request);

	TSharedPtr<FJsonObject> RequestObject = MakeShareable(new FJsonObject);
	RequestObject->SetStringField(TEXT("model"), Request.Model);

	TArray<TSharedPtr<FJsonValue>> Messages;
	{
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), SystemPrompt);
		Messages.Add(MakeShareable(new FJsonValueObject(SystemMessage)));
	}
	{
		TSharedPtr<FJsonObject> UserMessage = MakeShareable(new FJsonObject);
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField(TEXT("content"), UserPrompt);
		Messages.Add(MakeShareable(new FJsonValueObject(UserMessage)));
	}
	RequestObject->SetArrayField(TEXT("messages"), Messages);

	if (Request.ResponseFormat.Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{
		TSharedPtr<FJsonObject> ResponseFormatObject = MakeShareable(new FJsonObject);
		ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_object"));
		RequestObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
	}

	FString RequestBody;
	SerializeJsonObject(RequestObject, RequestBody);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Request.ApiKey));
	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->SetTimeout(Request.TimeoutSeconds);

	FHttpResponsePtr HttpResponse;
	bool bRequestCompleted = false;
	bool bRequestSucceeded = false;
	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[&HttpResponse, &bRequestCompleted, &bRequestSucceeded, CompletionEvent](FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bSucceeded)
		{
			HttpResponse = InResponse;
			bRequestSucceeded = bSucceeded;
			bRequestCompleted = true;
			CompletionEvent->Trigger();
		});

	if (!HttpRequest->ProcessRequest())
	{
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_REQUEST_FAILED");
		OutErrorMessage = TEXT("Failed to start the HTTP request");
		return false;
	}

	const double TimeoutAt = FPlatformTime::Seconds() + static_cast<double>(Request.TimeoutSeconds);
	while (!bRequestCompleted && FPlatformTime::Seconds() < TimeoutAt)
	{
		CompletionEvent->Wait(100);
	}

	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

	if (!bRequestCompleted)
	{
		HttpRequest->CancelRequest();
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_TIMEOUT");
		OutErrorMessage = TEXT("External AI request timed out");
		return false;
	}

	if (!bRequestSucceeded || !HttpResponse.IsValid())
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_REQUEST_FAILED");
		OutErrorMessage = TEXT("External AI request failed before a valid response was received");
		return false;
	}

	OutResponse.HttpStatus = HttpResponse->GetResponseCode();
	if (OutResponse.HttpStatus < 200 || OutResponse.HttpStatus >= 300)
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_REQUEST_FAILED");
		OutErrorMessage = FString::Printf(TEXT("HTTP %d: %s"), OutResponse.HttpStatus, *HttpResponse->GetContentAsString());
		return false;
	}

	TSharedPtr<FJsonObject> ResponseObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, ResponseObject) || !ResponseObject.IsValid())
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_INVALID_RESPONSE");
		OutErrorMessage = TEXT("Failed to parse the external AI JSON response");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!ResponseObject->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_INVALID_RESPONSE");
		OutErrorMessage = TEXT("Response did not contain any choices");
		return false;
	}

	const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
	if (!(*Choices)[0].IsValid() || !(*Choices)[0]->TryGetObject(ChoiceObject) || !ChoiceObject || !(*ChoiceObject).IsValid())
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_INVALID_RESPONSE");
		OutErrorMessage = TEXT("The first response choice was invalid");
		return false;
	}

	OutResponse.ProviderName = GetProviderName();
	OutResponse.Model = Request.Model;
	OutResponse.ContentText = ExtractContentString(*ChoiceObject);
	const TSharedPtr<FJsonObject>* UsageObject = nullptr;
	if (ResponseObject->TryGetObjectField(TEXT("usage"), UsageObject) && UsageObject && (*UsageObject).IsValid())
	{
		OutResponse.Usage = *UsageObject;
	}

	if (OutResponse.ContentText.IsEmpty())
	{
		OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_INVALID_RESPONSE");
		OutErrorMessage = TEXT("The response did not include any content");
		return false;
	}

	if (Request.ResponseFormat.Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{
		TSharedRef<TJsonReader<>> ContentReader = TJsonReaderFactory<>::Create(OutResponse.ContentText);
		if (!FJsonSerializer::Deserialize(ContentReader, OutResponse.ContentJson) || !OutResponse.ContentJson.IsValid())
		{
			OutErrorCode = TEXT("UEBMCP_EXTERNAL_AI_INVALID_RESPONSE");
			OutErrorMessage = TEXT("The model did not return valid JSON content");
			return false;
		}
	}

	return true;
}
