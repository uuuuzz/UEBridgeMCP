// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/ExternalAI/GenerateExternalAssetTool.h"

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

	FString BuildAssetPrompt(const FString& AssetType, const FString& Brief, const FString& ReferencePrompt, const FString& ImportDestination, const TSharedPtr<FJsonObject>& Metadata)
	{
		FString Prompt = FString::Printf(
			TEXT("Generate an external asset plan and payload for Unreal Engine.\nAsset type: %s\nBrief:\n%s\nReturn machine-readable JSON when possible, including files, suggested import settings, and any external URLs or base64 payloads you can provide."),
			*AssetType,
			*Brief);

		if (!ImportDestination.IsEmpty())
		{
			Prompt += FString::Printf(TEXT("\nSuggested Unreal import destination: %s"), *ImportDestination);
		}
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

	bool IsSupportedAssetType(const FString& AssetType)
	{
		return AssetType == TEXT("image")
			|| AssetType == TEXT("texture")
			|| AssetType == TEXT("material")
			|| AssetType == TEXT("mesh")
			|| AssetType == TEXT("static_mesh")
			|| AssetType == TEXT("audio")
			|| AssetType == TEXT("ui_image")
			|| AssetType == TEXT("json")
			|| AssetType == TEXT("text");
	}
}

FString UGenerateExternalAssetTool::GetToolDescription() const
{
	return TEXT("Generate an external asset payload or import plan through the configured HTTP provider. This returns generation output and an Unreal import plan; it does not import files by itself.");
}

TMap<FString, FMcpSchemaProperty> UGenerateExternalAssetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_type"), FMcpSchemaProperty::MakeEnum(TEXT("External asset type"), { TEXT("image"), TEXT("texture"), TEXT("material"), TEXT("mesh"), TEXT("static_mesh"), TEXT("audio"), TEXT("ui_image"), TEXT("json"), TEXT("text") }, true));
	Schema.Add(TEXT("brief"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Asset brief describing what to generate"), true));
	Schema.Add(TEXT("reference_prompt"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional reference prompt or style guidance")));
	Schema.Add(TEXT("import_destination"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Suggested Unreal content destination, e.g. /Game/Generated")));
	Schema.Add(TEXT("metadata"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional structured metadata for the provider")));
	Schema.Add(TEXT("provider"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional provider override; defaults to settings")));
	Schema.Add(TEXT("api_base_url"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional API base URL override")));
	Schema.Add(TEXT("api_key"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional API key override")));
	Schema.Add(TEXT("model"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional model override")));
	Schema.Add(TEXT("timeout_seconds"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional HTTP timeout override")));
	return Schema;
}

FMcpToolResult UGenerateExternalAssetTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetType = GetStringArgOrDefault(Arguments, TEXT("asset_type")).ToLower();
	const FString Brief = GetStringArgOrDefault(Arguments, TEXT("brief"));
	const FString ReferencePrompt = GetStringArgOrDefault(Arguments, TEXT("reference_prompt"));
	const FString ImportDestination = GetStringArgOrDefault(Arguments, TEXT("import_destination"));

	if (AssetType.IsEmpty() || Brief.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_type' and 'brief' are required"));
	}
	if (!IsSupportedAssetType(AssetType))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("Unsupported asset_type"));
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
	Request.ResponseFormat = TEXT("json");
	Request.Metadata = (MetadataObject && MetadataObject->IsValid()) ? *MetadataObject : nullptr;
	Request.TimeoutSeconds = GetFloatArgOrDefault(Arguments, TEXT("timeout_seconds"), Settings ? Settings->RequestTimeoutSeconds : 60.0f);
	Request.EffectivePrompt = BuildAssetPrompt(AssetType, Brief, ReferencePrompt, ImportDestination, Request.Metadata);

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

	TSharedPtr<FJsonObject> ImportPlan = MakeShareable(new FJsonObject);
	ImportPlan->SetStringField(TEXT("status"), TEXT("not_imported"));
	ImportPlan->SetStringField(TEXT("reason"), TEXT("generate-external-asset returns provider output and an import plan only; use import-assets or a dedicated importer after materializing files."));
	ImportPlan->SetStringField(TEXT("suggested_destination"), ImportDestination);
	ImportPlan->SetStringField(TEXT("asset_type"), AssetType);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("provider"), ProviderResponse.ProviderName);
	Response->SetStringField(TEXT("model"), ProviderResponse.Model);
	Response->SetStringField(TEXT("asset_type"), AssetType);
	Response->SetStringField(TEXT("brief"), Brief);
	Response->SetStringField(TEXT("effective_prompt"), Request.EffectivePrompt);
	Response->SetStringField(TEXT("content_text"), ProviderResponse.ContentText);
	Response->SetNumberField(TEXT("http_status"), ProviderResponse.HttpStatus);
	Response->SetObjectField(TEXT("import_plan"), ImportPlan);

	if (ProviderResponse.ContentJson.IsValid())
	{
		Response->SetObjectField(TEXT("content_json"), ProviderResponse.ContentJson);
	}
	if (ProviderResponse.Usage.IsValid())
	{
		Response->SetObjectField(TEXT("usage"), ProviderResponse.Usage);
	}
	if (Request.Metadata.IsValid())
	{
		Response->SetObjectField(TEXT("metadata"), Request.Metadata);
	}

	return FMcpToolResult::StructuredJson(Response);
}
