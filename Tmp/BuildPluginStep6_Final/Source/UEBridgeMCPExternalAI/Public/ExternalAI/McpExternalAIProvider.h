// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FExternalAIRequest
{
	FString ProviderName;
	FString ApiBaseUrl;
	FString ApiKey;
	FString Model;
	FString Brief;
	FString ReferencePrompt;
	FString EffectivePrompt;
	FString ResponseFormat;
	TSharedPtr<FJsonObject> Metadata;
	float TimeoutSeconds = 60.0f;
};

struct FExternalAIResponse
{
	FString ProviderName;
	FString Model;
	FString ContentText;
	TSharedPtr<FJsonObject> ContentJson;
	TSharedPtr<FJsonObject> Usage;
	int32 HttpStatus = 0;
};

class UEBRIDGEMCPEXTERNALAI_API IMcpExternalAIProvider
{
public:
	virtual ~IMcpExternalAIProvider() = default;

	virtual FString GetProviderName() const = 0;
	virtual bool IsConfigured(const FExternalAIRequest& Request, FString& OutReason) const = 0;
	virtual bool Generate(
		const FExternalAIRequest& Request,
		FExternalAIResponse& OutResponse,
		FString& OutErrorCode,
		FString& OutErrorMessage) = 0;
};
