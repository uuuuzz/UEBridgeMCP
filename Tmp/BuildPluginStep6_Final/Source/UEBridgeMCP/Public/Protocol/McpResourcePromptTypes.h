// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct UEBRIDGEMCP_API FMcpResourceContent
{
	FString Uri;
	FString Name;
	FString Description;
	FString MimeType = TEXT("text/plain");
	FString Text;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct UEBRIDGEMCP_API FMcpResourceReadResult
{
	TArray<FMcpResourceContent> Contents;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct UEBRIDGEMCP_API FMcpPromptArgumentDefinition
{
	FString Name;
	FString Description;
	bool bRequired = false;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct UEBRIDGEMCP_API FMcpPromptMessage
{
	FString Role = TEXT("user");
	FString ContentType = TEXT("text");
	FString Text;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct UEBRIDGEMCP_API FMcpPromptGetResult
{
	FString Name;
	FString Description;
	TArray<FMcpPromptMessage> Messages;

	TSharedPtr<FJsonObject> ToJson() const;
};

using FMcpResourceReadCallback = TFunction<bool(FMcpResourceReadResult& OutResult, FString& OutError)>;
using FMcpPromptBuildCallback = TFunction<bool(const TSharedPtr<FJsonObject>& Arguments, FMcpPromptGetResult& OutResult, FString& OutError)>;

struct UEBRIDGEMCP_API FMcpResourceDefinition
{
	FString Uri;
	FString Name;
	FString Description;
	FString MimeType = TEXT("text/plain");
	FMcpResourceReadCallback ReadCallback;

	TSharedPtr<FJsonObject> ToJson() const;
};

struct UEBRIDGEMCP_API FMcpPromptDefinition
{
	FString Name;
	FString Description;
	TArray<FMcpPromptArgumentDefinition> Arguments;
	FMcpPromptBuildCallback BuildCallback;

	TSharedPtr<FJsonObject> ToJson() const;
};
