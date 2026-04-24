// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Protocol/McpResourcePromptTypes.h"

TSharedPtr<FJsonObject> FMcpResourceContent::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("uri"), Uri);
	if (!Name.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("name"), Name);
	}
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}
	if (!MimeType.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("mimeType"), MimeType);
	}
	JsonObject->SetStringField(TEXT("text"), Text);
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpResourceReadResult::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> ContentsArray;
	for (const FMcpResourceContent& Content : Contents)
	{
		ContentsArray.Add(MakeShareable(new FJsonValueObject(Content.ToJson())));
	}
	JsonObject->SetArrayField(TEXT("contents"), ContentsArray);
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptArgumentDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("name"), Name);
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}
	JsonObject->SetBoolField(TEXT("required"), bRequired);
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptMessage::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("role"), Role);

	TSharedPtr<FJsonObject> ContentObject = MakeShareable(new FJsonObject);
	ContentObject->SetStringField(TEXT("type"), ContentType);
	ContentObject->SetStringField(TEXT("text"), Text);
	JsonObject->SetObjectField(TEXT("content"), ContentObject);
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptGetResult::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	if (!Name.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("name"), Name);
	}
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FMcpPromptMessage& Message : Messages)
	{
		MessagesArray.Add(MakeShareable(new FJsonValueObject(Message.ToJson())));
	}
	JsonObject->SetArrayField(TEXT("messages"), MessagesArray);
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpResourceDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("uri"), Uri);
	JsonObject->SetStringField(TEXT("name"), Name);
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}
	if (!MimeType.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("mimeType"), MimeType);
	}
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("name"), Name);
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}

	TArray<TSharedPtr<FJsonValue>> ArgumentsArray;
	for (const FMcpPromptArgumentDefinition& Argument : Arguments)
	{
		ArgumentsArray.Add(MakeShareable(new FJsonValueObject(Argument.ToJson())));
	}
	JsonObject->SetArrayField(TEXT("arguments"), ArgumentsArray);
	return JsonObject;
}
