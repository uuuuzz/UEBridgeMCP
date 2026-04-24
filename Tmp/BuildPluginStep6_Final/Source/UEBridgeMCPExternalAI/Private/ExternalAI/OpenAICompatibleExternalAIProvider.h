// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ExternalAI/McpExternalAIProvider.h"

class FOpenAICompatibleExternalAIProvider final : public IMcpExternalAIProvider
{
public:
	virtual FString GetProviderName() const override { return TEXT("openai_compatible"); }
	virtual bool IsConfigured(const FExternalAIRequest& Request, FString& OutReason) const override;
	virtual bool Generate(
		const FExternalAIRequest& Request,
		FExternalAIResponse& OutResponse,
		FString& OutErrorCode,
		FString& OutErrorMessage) override;
};
