// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Protocol/McpTypes.h"
#include "Tools/McpToolResult.h"
#include "McpToolBase.generated.h"

/**
 * Tool execution context
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpToolContext
{
	GENERATED_BODY()

	/** MCP session ID */
	FString SessionId;

	/** Request ID for progress reporting */
	FString RequestId;

	/** Cancellation token */
	TSharedPtr<FThreadSafeBool> CancellationToken;

	/** Progress callback */
	TFunction<void(float Progress, const FString& Message)> OnProgress;

	/** Check if cancellation was requested */
	bool IsCancelled() const
	{
		return CancellationToken.IsValid() && (*CancellationToken);
	}
};

/**
 * Abstract base class for MCP tools
 */
UCLASS(Abstract)
class UEBRIDGEMCP_API UMcpToolBase : public UObject
{
	GENERATED_BODY()

public:
	virtual FString GetToolKind() const { return TEXT("query"); }
	virtual FString GetResourceScope() const { return TEXT("generic"); }
	virtual bool MutatesState() const { return false; }
	virtual bool SupportsBatch() const { return false; }
	virtual bool SupportsDryRun() const { return false; }
	virtual bool SupportsCompile() const { return false; }
	virtual bool SupportsSave() const { return false; }

	/** Get tool name (unique identifier) */
	virtual FString GetToolName() const PURE_VIRTUAL(UMcpToolBase::GetToolName, return TEXT(""););

	/** Get tool description */
	virtual FString GetToolDescription() const PURE_VIRTUAL(UMcpToolBase::GetToolDescription, return TEXT(""););

	/** Get tool definition for tools/list */
	virtual FMcpToolDefinition GetDefinition() const;

	/** Get input schema properties */
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const { return {}; }

	/** Get required parameter names */
	virtual TArray<FString> GetRequiredParams() const { return {}; }

	/**
	 * Execute the tool
	 * This is called from the HTTP server thread - implementations should
	 * handle game thread synchronization if needed using AsyncTask
	 */
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) PURE_VIRTUAL(UMcpToolBase::Execute, return FMcpToolResult::Error(TEXT("Not implemented")););

	/** Whether this tool requires game thread execution */
	virtual bool RequiresGameThread() const { return true; }

protected:
	/** Helper to get string argument */
	static bool GetStringArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, FString& OutValue);

	/** Helper to get optional string argument with default */
	static FString GetStringArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, const FString& Default = TEXT(""));

	/** Helper to get bool argument */
	static bool GetBoolArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool& OutValue);

	/** Helper to get optional bool argument with default */
	static bool GetBoolArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool Default = false);

	/** Helper to get int argument */
	static bool GetIntArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32& OutValue);

	/** Helper to get optional int argument with default */
	static int32 GetIntArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32 Default = 0);

	/** Helper to get float argument */
	static bool GetFloatArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, float& OutValue);

	/** Helper to get optional float argument with default */
	static float GetFloatArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, float Default = 0.0f);
};

/**
 * Macro for easy tool registration
 * Use in .cpp file after includes:
 * REGISTER_MCP_TOOL(UMyTool)
 */
#define REGISTER_MCP_TOOL(ToolClass) \
	static struct F##ToolClass##Registrar \
	{ \
		F##ToolClass##Registrar() \
		{ \
			FMcpToolRegistry::Get().RegisterToolClass(ToolClass::StaticClass()); \
		} \
	} G##ToolClass##Registrar;

// Forward declaration for the registry
class FMcpToolRegistry;
