// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "McpExternalAISettings.generated.h"

UCLASS(config=UEBridgeMCP, defaultconfig)
class UEBRIDGEMCPEXTERNALAI_API UMcpExternalAISettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category="External AI")
	bool bEnableExternalAI = false;

	UPROPERTY(config, EditAnywhere, Category="External AI")
	FString ProviderName = TEXT("openai_compatible");

	UPROPERTY(config, EditAnywhere, Category="External AI")
	FString ApiBaseUrl = TEXT("https://api.openai.com/v1");

	UPROPERTY(config, EditAnywhere, Category="External AI")
	FString ApiKey;

	UPROPERTY(config, EditAnywhere, Category="External AI")
	FString DefaultModel;

	UPROPERTY(config, EditAnywhere, Category="External AI", meta=(ClampMin="1.0", ClampMax="300.0"))
	float RequestTimeoutSeconds = 60.0f;
};
