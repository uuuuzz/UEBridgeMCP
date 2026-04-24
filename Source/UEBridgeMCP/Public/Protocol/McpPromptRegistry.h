// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Protocol/McpResourcePromptTypes.h"

class UEBRIDGEMCP_API FMcpPromptRegistry
{
public:
	static FMcpPromptRegistry& Get();
	~FMcpPromptRegistry() = default;

	void RegisterPrompt(const FMcpPromptDefinition& Definition);
	void UnregisterPrompt(const FString& Name);
	void ClearAllPrompts();

	TArray<FMcpPromptDefinition> GetAllPromptDefinitions() const;
	bool BuildPrompt(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, FMcpPromptGetResult& OutResult, FString& OutError) const;
	bool HasPrompt(const FString& Name) const;

private:
	FMcpPromptRegistry() = default;

	FMcpPromptRegistry(const FMcpPromptRegistry&) = delete;
	FMcpPromptRegistry& operator=(const FMcpPromptRegistry&) = delete;

	mutable FCriticalSection Lock;
	TMap<FString, FMcpPromptDefinition> Prompts;

	static FMcpPromptRegistry* Instance;
};
