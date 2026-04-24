// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Protocol/McpPromptRegistry.h"

#include "UEBridgeMCP.h"

FMcpPromptRegistry* FMcpPromptRegistry::Instance = nullptr;

FMcpPromptRegistry& FMcpPromptRegistry::Get()
{
	if (!Instance)
	{
		Instance = new FMcpPromptRegistry();
	}
	return *Instance;
}

void FMcpPromptRegistry::RegisterPrompt(const FMcpPromptDefinition& Definition)
{
	if (Definition.Name.IsEmpty() || !Definition.BuildCallback)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Attempted to register invalid MCP prompt"));
		return;
	}

	FScopeLock ScopeLock(&Lock);
	Prompts.Add(Definition.Name, Definition);
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Registered MCP prompt: %s"), *Definition.Name);
}

void FMcpPromptRegistry::UnregisterPrompt(const FString& Name)
{
	FScopeLock ScopeLock(&Lock);
	Prompts.Remove(Name);
}

void FMcpPromptRegistry::ClearAllPrompts()
{
	FScopeLock ScopeLock(&Lock);
	Prompts.Empty();
}

TArray<FMcpPromptDefinition> FMcpPromptRegistry::GetAllPromptDefinitions() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FMcpPromptDefinition> Definitions;
	Prompts.GenerateValueArray(Definitions);
	return Definitions;
}

bool FMcpPromptRegistry::BuildPrompt(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, FMcpPromptGetResult& OutResult, FString& OutError) const
{
	FMcpPromptDefinition Definition;
	{
		FScopeLock ScopeLock(&Lock);
		const FMcpPromptDefinition* DefinitionPtr = Prompts.Find(Name);
		if (!DefinitionPtr)
		{
			OutError = FString::Printf(TEXT("Prompt not found: %s"), *Name);
			return false;
		}
		Definition = *DefinitionPtr;
	}

	if (!Definition.BuildCallback)
	{
		OutError = FString::Printf(TEXT("Prompt has no build callback: %s"), *Name);
		return false;
	}

	return Definition.BuildCallback(Arguments, OutResult, OutError);
}

bool FMcpPromptRegistry::HasPrompt(const FString& Name) const
{
	FScopeLock ScopeLock(&Lock);
	return Prompts.Contains(Name);
}
