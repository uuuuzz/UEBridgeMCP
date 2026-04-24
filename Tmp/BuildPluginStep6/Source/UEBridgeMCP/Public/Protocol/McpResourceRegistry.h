// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Protocol/McpResourcePromptTypes.h"

class UEBRIDGEMCP_API FMcpResourceRegistry
{
public:
	static FMcpResourceRegistry& Get();
	~FMcpResourceRegistry() = default;

	void RegisterResource(const FMcpResourceDefinition& Definition);
	void UnregisterResource(const FString& Uri);
	void ClearAllResources();

	TArray<FMcpResourceDefinition> GetAllResourceDefinitions() const;
	bool ReadResource(const FString& Uri, FMcpResourceReadResult& OutResult, FString& OutError) const;
	bool HasResource(const FString& Uri) const;

private:
	FMcpResourceRegistry() = default;

	FMcpResourceRegistry(const FMcpResourceRegistry&) = delete;
	FMcpResourceRegistry& operator=(const FMcpResourceRegistry&) = delete;

	mutable FCriticalSection Lock;
	TMap<FString, FMcpResourceDefinition> Resources;

	static FMcpResourceRegistry* Instance;
};
