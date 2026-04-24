// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Protocol/McpCapabilities.h"

TSharedPtr<FJsonObject> FMcpServerCapabilities::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	if (bSupportsTools)
	{
		TSharedPtr<FJsonObject> ToolsObj = MakeShareable(new FJsonObject);
		ToolsObj->SetBoolField(TEXT("listChanged"), bToolsListChanged);
		ToolsObj->SetBoolField(TEXT("list"), bSupportsToolListing);
		ToolsObj->SetBoolField(TEXT("call"), bSupportsToolInvocation);
		ToolsObj->SetNumberField(TEXT("registeredCount"), RegisteredToolCount);
		JsonObject->SetObjectField(TEXT("tools"), ToolsObj);
	}

	if (bSupportsResources)
	{
		TSharedPtr<FJsonObject> ResourcesObj = MakeShareable(new FJsonObject);
		JsonObject->SetObjectField(TEXT("resources"), ResourcesObj);
	}

	if (bSupportsPrompts)
	{
		TSharedPtr<FJsonObject> PromptsObj = MakeShareable(new FJsonObject);
		JsonObject->SetObjectField(TEXT("prompts"), PromptsObj);
	}

	if (bSupportsLogging)
	{
		TSharedPtr<FJsonObject> LoggingObj = MakeShareable(new FJsonObject);
		JsonObject->SetObjectField(TEXT("logging"), LoggingObj);
	}

	return JsonObject;
}

FMcpClientCapabilities FMcpClientCapabilities::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FMcpClientCapabilities Capabilities;

	if (!JsonObject.IsValid())
	{
		return Capabilities;
	}

	const TSharedPtr<FJsonObject>* RootsObj;
	if (JsonObject->TryGetObjectField(TEXT("roots"), RootsObj))
	{
		Capabilities.bSupportsRoots = true;
		(*RootsObj)->TryGetBoolField(TEXT("listChanged"), Capabilities.bRootsListChanged);
	}

	const TSharedPtr<FJsonObject>* SamplingObj;
	if (JsonObject->TryGetObjectField(TEXT("sampling"), SamplingObj))
	{
		Capabilities.bSupportsSampling = true;
	}

	return Capabilities;
}

TSharedPtr<FJsonObject> FMcpServerInfo::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetStringField(TEXT("name"), Name);
	JsonObject->SetStringField(TEXT("version"), Version);
	return JsonObject;
}

FMcpClientInfo FMcpClientInfo::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FMcpClientInfo Info;

	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("name"), Info.Name);
		JsonObject->TryGetStringField(TEXT("version"), Info.Version);
	}

	return Info;
}
