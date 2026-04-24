// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Protocol/McpTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

TOptional<FMcpRequest> FMcpRequest::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return TOptional<FMcpRequest>();
	}

	FMcpRequest Request;

	// Parse jsonrpc
	if (!JsonObject->TryGetStringField(TEXT("jsonrpc"), Request.JsonRpc) || Request.JsonRpc != JSONRPC_VERSION)
	{
		return TOptional<FMcpRequest>();
	}

	// Parse method (required)
	if (!JsonObject->TryGetStringField(TEXT("method"), Request.Method))
	{
		return TOptional<FMcpRequest>();
	}

	// Parse id (optional for notifications)
	// JSON-RPC 2.0 allows id to be string, number, or null
	if (!JsonObject->TryGetStringField(TEXT("id"), Request.Id))
	{
		// Try as number
		double IdNumber;
		if (JsonObject->TryGetNumberField(TEXT("id"), IdNumber))
		{
			Request.Id = FString::Printf(TEXT("%.0f"), IdNumber);
		}
	}

	// Parse params (optional)
	const TSharedPtr<FJsonObject>* ParamsObject;
	if (JsonObject->TryGetObjectField(TEXT("params"), ParamsObject))
	{
		Request.Params = *ParamsObject;
	}

	// Parse method enum
	if (Request.Method == TEXT("initialize"))
	{
		Request.ParsedMethod = EMcpMethod::Initialize;
	}
	else if (Request.Method == TEXT("notifications/initialized"))
	{
		Request.ParsedMethod = EMcpMethod::Initialized;
	}
	else if (Request.Method == TEXT("shutdown"))
	{
		Request.ParsedMethod = EMcpMethod::Shutdown;
	}
	else if (Request.Method == TEXT("tools/list"))
	{
		Request.ParsedMethod = EMcpMethod::ToolsList;
	}
	else if (Request.Method == TEXT("tools/call"))
	{
		Request.ParsedMethod = EMcpMethod::ToolsCall;
	}
	else if (Request.Method == TEXT("resources/list"))
	{
		Request.ParsedMethod = EMcpMethod::ResourcesList;
	}
	else if (Request.Method == TEXT("resources/read"))
	{
		Request.ParsedMethod = EMcpMethod::ResourcesRead;
	}
	else if (Request.Method == TEXT("prompts/list"))
	{
		Request.ParsedMethod = EMcpMethod::PromptsList;
	}
	else if (Request.Method == TEXT("prompts/get"))
	{
		Request.ParsedMethod = EMcpMethod::PromptsGet;
	}
	else if (Request.Method == TEXT("notifications/cancelled"))
	{
		Request.ParsedMethod = EMcpMethod::CancelledNotification;
	}
	else if (Request.Method == TEXT("notifications/progress"))
	{
		Request.ParsedMethod = EMcpMethod::ProgressNotification;
	}
	else
	{
		Request.ParsedMethod = EMcpMethod::Unknown;
	}

	return Request;
}

TOptional<FMcpRequest> FMcpRequest::FromJsonString(const FString& JsonString)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		return FromJson(JsonObject);
	}

	return TOptional<FMcpRequest>();
}

FMcpResponse FMcpResponse::Success(const FString& InId, TSharedPtr<FJsonObject> InResult)
{
	FMcpResponse Response;
	Response.Id = InId;
	Response.Result = InResult;
	Response.ErrorData = nullptr;
	return Response;
}

FMcpResponse FMcpResponse::Error(const FString& InId, int32 Code, const FString& Message,
                                  TSharedPtr<FJsonObject> Data)
{
	FMcpResponse Response;
	Response.Id = InId;
	Response.Result = nullptr;

	Response.ErrorData = MakeShareable(new FJsonObject);
	Response.ErrorData->SetNumberField(TEXT("code"), Code);
	Response.ErrorData->SetStringField(TEXT("message"), Message);
	if (Data.IsValid())
	{
		Response.ErrorData->SetObjectField(TEXT("data"), Data);
	}

	return Response;
}

TSharedPtr<FJsonObject> FMcpResponse::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("jsonrpc"), JsonRpc);
	JsonObject->SetStringField(TEXT("id"), Id);

	if (Result.IsValid())
	{
		JsonObject->SetObjectField(TEXT("result"), Result);
	}
	else if (ErrorData.IsValid())
	{
		JsonObject->SetObjectField(TEXT("error"), ErrorData);
	}

	return JsonObject;
}

FString FMcpResponse::ToJsonString() const
{
	TSharedPtr<FJsonObject> JsonObject = ToJson();
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	return OutputString;
}

/** 最大递归深度，防止循环引用导致栈溢出 */
static constexpr int32 MaxSchemaRecursionDepth = 16;

TSharedPtr<FJsonObject> FMcpSchemaProperty::ToJson() const
{
	return ToJsonWithDepth(0);
}

TSharedPtr<FJsonObject> FMcpSchemaProperty::ToJsonWithDepth(int32 CurrentDepth) const
{
	if (CurrentDepth > MaxSchemaRecursionDepth)
	{
		// 超过最大递归深度，返回一个带警告的空 Schema，避免栈溢出崩溃
		TSharedPtr<FJsonObject> Fallback = MakeShareable(new FJsonObject);
		Fallback->SetStringField(TEXT("description"), TEXT("[Schema truncated: max recursion depth exceeded]"));
		return Fallback;
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	// If RawSchema is provided, merge it directly and return
	if (RawSchema.IsValid())
	{
		for (const auto& Pair : RawSchema->Values)
		{
			JsonObject->SetField(Pair.Key, Pair.Value);
		}
		// Still emit description if not already in RawSchema
		if (!JsonObject->HasField(TEXT("description")) && !Description.IsEmpty())
		{
			JsonObject->SetStringField(TEXT("description"), Description);
		}
		return JsonObject;
	}

	// Only set type if it's not empty and not "any"
	// In JSON Schema, omitting "type" means any type is valid
	if (!Type.IsEmpty() && Type != TEXT("any"))
	{
		JsonObject->SetStringField(TEXT("type"), Type);
	}

	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}

	if (!Format.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("format"), Format);
	}

	if (Enum.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> EnumValues;
		for (const FString& EnumValue : Enum)
		{
			EnumValues.Add(MakeShareable(new FJsonValueString(EnumValue)));
		}
		JsonObject->SetArrayField(TEXT("enum"), EnumValues);
	}

	if (Default.IsValid())
	{
		JsonObject->SetField(TEXT("default"), Default);
	}

	// Nested object properties
	if (Type == TEXT("object") && Properties.Num() > 0)
	{
		TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);
		for (const auto& Pair : Properties)
		{
			if (Pair.Value.IsValid())
			{
				PropsObj->SetObjectField(Pair.Key, Pair.Value->ToJsonWithDepth(CurrentDepth + 1));
			}
		}
		JsonObject->SetObjectField(TEXT("properties"), PropsObj);

		if (NestedRequired.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> RequiredArray;
			for (const FString& Req : NestedRequired)
			{
				RequiredArray.Add(MakeShareable(new FJsonValueString(Req)));
			}
			JsonObject->SetArrayField(TEXT("required"), RequiredArray);
		}

		if (!bAdditionalProperties)
		{
			JsonObject->SetBoolField(TEXT("additionalProperties"), false);
		}
	}

	// Array items schema
	if (Type == TEXT("array"))
	{
		if (Items.IsValid())
		{
			// Rich items schema (array<object> or array<nested>)
			JsonObject->SetObjectField(TEXT("items"), Items->ToJsonWithDepth(CurrentDepth + 1));
		}
		else if (!ItemsType.IsEmpty())
		{
			// Simple items type (backward compatible)
			TSharedPtr<FJsonObject> ItemsObj = MakeShareable(new FJsonObject);
			ItemsObj->SetStringField(TEXT("type"), ItemsType);
			JsonObject->SetObjectField(TEXT("items"), ItemsObj);
		}
	}

	// Examples
	if (Examples.Num() > 0)
	{
		JsonObject->SetArrayField(TEXT("examples"), Examples);
	}

	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpToolDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("name"), Name);
	JsonObject->SetStringField(TEXT("description"), Description);

	// Build inputSchema
	TSharedPtr<FJsonObject> InputSchemaObj = MakeShareable(new FJsonObject);
	InputSchemaObj->SetStringField(TEXT("type"), TEXT("object"));

	// Properties
	TSharedPtr<FJsonObject> PropertiesObj = MakeShareable(new FJsonObject);
	for (const auto& Pair : InputSchema)
	{
		PropertiesObj->SetObjectField(Pair.Key, Pair.Value.ToJson());
	}
	InputSchemaObj->SetObjectField(TEXT("properties"), PropertiesObj);

	// Required fields
	if (Required.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RequiredArray;
		for (const FString& RequiredField : Required)
		{
			RequiredArray.Add(MakeShareable(new FJsonValueString(RequiredField)));
		}
		InputSchemaObj->SetArrayField(TEXT("required"), RequiredArray);
	}

	JsonObject->SetObjectField(TEXT("inputSchema"), InputSchemaObj);

	TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
	MetadataObject->SetStringField(TEXT("kind"), Kind);
	MetadataObject->SetStringField(TEXT("resource_scope"), ResourceScope);
	MetadataObject->SetBoolField(TEXT("mutates"), bMutates);
	MetadataObject->SetBoolField(TEXT("supports_batch"), bSupportsBatch);
	MetadataObject->SetBoolField(TEXT("supports_dry_run"), bSupportsDryRun);
	MetadataObject->SetBoolField(TEXT("supports_compile"), bSupportsCompile);
	MetadataObject->SetBoolField(TEXT("supports_save"), bSupportsSave);
	JsonObject->SetObjectField(TEXT("metadata"), MetadataObject);

	return JsonObject;
}
