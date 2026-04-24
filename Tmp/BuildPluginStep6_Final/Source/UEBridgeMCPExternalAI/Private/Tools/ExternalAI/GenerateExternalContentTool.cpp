// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/ExternalAI/GenerateExternalContentTool.h"

#include "ExternalAI/McpExternalAISettings.h"
#include "ExternalAI/OpenAICompatibleExternalAIProvider.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	FString BuildEffectivePrompt(const FString& Brief, const FString& ReferencePrompt, const TSharedPtr<FJsonObject>& Metadata)
	{
		FString Prompt = FString::Printf(TEXT("Create external content for this brief:\n%s"), *Brief);
		if (!ReferencePrompt.IsEmpty())
		{
			Prompt += FString::Printf(TEXT("\n\nReference prompt:\n%s"), *ReferencePrompt);
		}
		if (Metadata.IsValid())
		{
			Prompt += FString::Printf(TEXT("\n\nStructured metadata:\n%s"), *SerializeJsonObject(Metadata));
		}
		return Prompt;
	}
}

FString UGenerateExternalContentTool::GetToolDescription() const
{
	return TEXT("Generate external text or JSON content through an HTTP-based provider adapter, returning the brief, effective prompt, content payload, and structured metadata.");
}

TMap<FString, FMcpSchemaProperty> UGenerateExternalContentTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("brief"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Content brief describing what to generate"), true));
	Schema.Add(TEXT("reference_prompt"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional reference prompt or style guidance")));
	Schema.Add(TEXT("response_format"), FMcpSchemaProperty::MakeEnum(TEXT("Desired response format"), { TEXT("text"), TEXT("json") }));
	Schema.Add(TEXT("metadata"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional structured metadata passed through to the provider prompt")));
	Schema.Add(TEXT("provider"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional provider override; defaults to settings")));
	Schema.Add(TEXT("api_base_url"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional API base URL override")));
	Schema.Add(TEXT("api_key"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional API key override")));
	Schema.Add(TEXT("model"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional model override")));
	Schema.Add(TEXT("timeout_seconds"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional HTTP timeout override")));
	return Schema;
}

FMcpToolResult UGenerateExternalContentTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString Brief = GetStringArgOrDefault(Arguments, TEXT("brief"));
	const FString ReferencePrompt = GetStringArgOrDefault(Arguments, TEXT("reference_prompt"));
	const FString ResponseFormat = GetStringArgOrDefault(Arguments, TEXT("response_format"), TEXT("text")).ToLower();

	if (Brief.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'brief' is required"));
	}

	if (ResponseFormat != TEXT("text") && ResponseFormat != TEXT("json"))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'response_format' must be 'text' or 'json'"));
	}

	const UMcpExternalAISettings* Settings = GetDefault<UMcpExternalAISettings>();
	const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
	Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject);

	FExternalAIRequest Request;
	Request.ProviderName = GetStringArgOrDefault(Arguments, TEXT("provider"), Settings ? Settings->ProviderName : TEXT("openai_compatible"));
	Request.ApiBaseUrl = GetStringArgOrDefault(Arguments, TEXT("api_base_url"), Settings ? Settings->ApiBaseUrl : TEXT(""));
	Request.ApiKey = GetStringArgOrDefault(Arguments, TEXT("api_key"), Settings ? Settings->ApiKey : TEXT(""));
	Request.Model = GetStringArgOrDefault(Arguments, TEXT("model"), Settings ? Settings->DefaultModel : TEXT(""));
	Request.Brief = Brief;
	Request.ReferencePrompt = ReferencePrompt;
	Request.ResponseFormat = ResponseFormat;
	Request.Metadata = (MetadataObject && MetadataObject->IsValid()) ? *MetadataObject : nullptr;
	Request.TimeoutSeconds = GetFloatArgOrDefault(Arguments, TEXT("timeout_seconds"), Settings ? Settings->RequestTimeoutSeconds : 60.0f);
	Request.EffectivePrompt = BuildEffectivePrompt(Brief, ReferencePrompt, Request.Metadata);

	if (Settings && !Settings->bEnableExternalAI && GetStringArgOrDefault(Arguments, TEXT("api_key")).IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_EXTERNAL_AI_NOT_CONFIGURED"), TEXT("External AI is disabled in settings and no request-level API override was provided"));
	}

	if (!Request.ProviderName.Equals(TEXT("openai_compatible"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_EXTERNAL_AI_UNSUPPORTED_PROVIDER"), FString::Printf(TEXT("Unsupported provider '%s'"), *Request.ProviderName));
	}

	FOpenAICompatibleExternalAIProvider Provider;
	FExternalAIResponse ProviderResponse;
	FString ErrorCode;
	FString ErrorMessage;
	if (!Provider.Generate(Request, ProviderResponse, ErrorCode, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("provider"), ProviderResponse.ProviderName);
	Response->SetStringField(TEXT("model"), ProviderResponse.Model);
	Response->SetStringField(TEXT("brief"), Brief);
	Response->SetStringField(TEXT("reference_prompt"), ReferencePrompt);
	Response->SetStringField(TEXT("effective_prompt"), Request.EffectivePrompt);
	Response->SetStringField(TEXT("response_format"), ResponseFormat);
	Response->SetStringField(TEXT("content_text"), ProviderResponse.ContentText);
	Response->SetNumberField(TEXT("http_status"), ProviderResponse.HttpStatus);

	if (Request.Metadata.IsValid())
	{
		Response->SetObjectField(TEXT("metadata"), Request.Metadata);
	}

	if (ProviderResponse.ContentJson.IsValid())
	{
		Response->SetObjectField(TEXT("content_json"), ProviderResponse.ContentJson);
	}

	if (ProviderResponse.Usage.IsValid())
	{
		Response->SetObjectField(TEXT("usage"), ProviderResponse.Usage);
	}

	return FMcpToolResult::StructuredJson(Response);
}
