// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "GetLogsTool.generated.h"

/**
 * MCP tool to retrieve Unreal Engine output log entries for debugging.
 * Supports filtering by category, severity level, and message content.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UGetLogsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("get-logs"); }

	virtual FString GetToolDescription() const override
	{
		return TEXT("Retrieve Unreal Engine output log entries for debugging. "
			"Supports filtering by category, severity level, and message content.");
	}

	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	/**
	 * Parse verbosity string to ELogVerbosity.
	 */
	static ELogVerbosity::Type ParseVerbosity(const FString& VerbosityStr);

	/**
	 * Convert ELogVerbosity to string.
	 */
	static FString VerbosityToString(ELogVerbosity::Type Verbosity);
};
