// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Protocol/McpResourceRegistry.h"

#include "UEBridgeMCP.h"

FMcpResourceRegistry* FMcpResourceRegistry::Instance = nullptr;

FMcpResourceRegistry& FMcpResourceRegistry::Get()
{
	if (!Instance)
	{
		Instance = new FMcpResourceRegistry();
	}
	return *Instance;
}

void FMcpResourceRegistry::RegisterResource(const FMcpResourceDefinition& Definition)
{
	if (Definition.Uri.IsEmpty() || Definition.Name.IsEmpty() || !Definition.ReadCallback)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Attempted to register invalid MCP resource"));
		return;
	}

	FScopeLock ScopeLock(&Lock);
	Resources.Add(Definition.Uri, Definition);
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Registered MCP resource: %s"), *Definition.Uri);
}

void FMcpResourceRegistry::UnregisterResource(const FString& Uri)
{
	FScopeLock ScopeLock(&Lock);
	Resources.Remove(Uri);
}

void FMcpResourceRegistry::ClearAllResources()
{
	FScopeLock ScopeLock(&Lock);
	Resources.Empty();
}

TArray<FMcpResourceDefinition> FMcpResourceRegistry::GetAllResourceDefinitions() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FMcpResourceDefinition> Definitions;
	Resources.GenerateValueArray(Definitions);
	return Definitions;
}

bool FMcpResourceRegistry::ReadResource(const FString& Uri, FMcpResourceReadResult& OutResult, FString& OutError) const
{
	FMcpResourceDefinition Definition;
	{
		FScopeLock ScopeLock(&Lock);
		const FMcpResourceDefinition* DefinitionPtr = Resources.Find(Uri);
		if (!DefinitionPtr)
		{
			OutError = FString::Printf(TEXT("Resource not found: %s"), *Uri);
			return false;
		}
		Definition = *DefinitionPtr;
	}

	if (!Definition.ReadCallback)
	{
		OutError = FString::Printf(TEXT("Resource has no read callback: %s"), *Uri);
		return false;
	}

	return Definition.ReadCallback(OutResult, OutError);
}

bool FMcpResourceRegistry::HasResource(const FString& Uri) const
{
	FScopeLock ScopeLock(&Lock);
	return Resources.Contains(Uri);
}
