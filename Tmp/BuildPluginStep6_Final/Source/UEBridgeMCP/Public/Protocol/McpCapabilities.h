// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "McpCapabilities.generated.h"

/**
 * Server capabilities to declare during initialization
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpServerCapabilities
{
	GENERATED_BODY()

	/** Server supports tools */
	UPROPERTY()
	bool bSupportsTools = true;

	/** Server supports tool list change notifications */
	UPROPERTY()
	bool bToolsListChanged = false;

	/** Server supports tools/list */
	UPROPERTY()
	bool bSupportsToolListing = true;

	/** Server supports tools/call */
	UPROPERTY()
	bool bSupportsToolInvocation = true;

	/** Number of tools currently registered */
	UPROPERTY()
	int32 RegisteredToolCount = 0;

	/** Server supports resources */
	UPROPERTY()
	bool bSupportsResources = false;

	/** Server supports prompts */
	UPROPERTY()
	bool bSupportsPrompts = false;

	/** Server supports logging */
	UPROPERTY()
	bool bSupportsLogging = false;

	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Client capabilities received during initialization
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpClientCapabilities
{
	GENERATED_BODY()

	/** Client supports roots */
	UPROPERTY()
	bool bSupportsRoots = false;

	/** Client supports roots list changed notifications */
	UPROPERTY()
	bool bRootsListChanged = false;

	/** Client supports sampling */
	UPROPERTY()
	bool bSupportsSampling = false;

	static FMcpClientCapabilities FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * Server information for initialize response
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpServerInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name = TEXT("ue-bridge-mcp");

	UPROPERTY()
	FString Version;

	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Client information received during initialization
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpClientInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Version;

	static FMcpClientInfo FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};
