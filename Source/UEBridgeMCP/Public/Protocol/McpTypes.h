// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "McpTypes.generated.h"

/** MCP Protocol version */
#define MCP_PROTOCOL_VERSION TEXT("2025-06-18")

/** JSON-RPC version */
#define JSONRPC_VERSION TEXT("2.0")

/**
 * MCP request methods
 */
UENUM()
enum class EMcpMethod : uint8
{
	// Lifecycle
	Initialize,
	Initialized,
	Shutdown,

	// Tools
	ToolsList,
	ToolsCall,

	// Resources (future)
	ResourcesList,
	ResourcesRead,
	ResourcesTemplatesList,
	ResourcesSubscribe,
	ResourcesUnsubscribe,

	// Prompts (future)
	PromptsList,
	PromptsGet,

	// Notifications
	CancelledNotification,
	ProgressNotification,
	ResourcesListChanged,
	ToolsListChanged,

	Unknown
};

/**
 * JSON-RPC error codes
 * https://www.jsonrpc.org/specification#error_object
 */
namespace EMcpErrorCode
{
	// JSON-RPC standard errors
	constexpr int32 ParseError = -32700;
	constexpr int32 InvalidRequest = -32600;
	constexpr int32 MethodNotFound = -32601;
	constexpr int32 InvalidParams = -32602;
	constexpr int32 InternalError = -32603;

	// MCP-specific errors
	constexpr int32 ServerError = -32000;
	constexpr int32 ClientError = -32001;
}

/**
 * JSON-RPC Request structure
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpRequest
{
	GENERATED_BODY()

	/** JSON-RPC version (always "2.0") */
	UPROPERTY()
	FString JsonRpc = JSONRPC_VERSION;

	/** Request ID (string or number, stored as string) */
	UPROPERTY()
	FString Id;

	/** Method name */
	UPROPERTY()
	FString Method;

	/** Parameters object */
	TSharedPtr<FJsonObject> Params;

	/** Parsed method enum for fast dispatch */
	EMcpMethod ParsedMethod = EMcpMethod::Unknown;

	/** Check if this is a notification (no ID) */
	bool IsNotification() const { return Id.IsEmpty(); }

	/** Parse from JSON object */
	static TOptional<FMcpRequest> FromJson(const TSharedPtr<FJsonObject>& JsonObject);

	/** Parse from JSON string */
	static TOptional<FMcpRequest> FromJsonString(const FString& JsonString);
};

/**
 * JSON-RPC Response structure
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpResponse
{
	GENERATED_BODY()

	/** JSON-RPC version */
	UPROPERTY()
	FString JsonRpc = JSONRPC_VERSION;

	/** Request ID (matches request) */
	UPROPERTY()
	FString Id;

	/** Result object (mutually exclusive with Error) */
	TSharedPtr<FJsonObject> Result;

	/** Error object (mutually exclusive with Result) */
	TSharedPtr<FJsonObject> ErrorData;

	/** Create success response */
	static FMcpResponse Success(const FString& InId, TSharedPtr<FJsonObject> InResult);

	/** Create error response */
	static FMcpResponse Error(const FString& InId, int32 Code, const FString& Message,
	                          TSharedPtr<FJsonObject> Data = nullptr);

	/** Serialize to JSON string */
	FString ToJsonString() const;

	/** Serialize to JSON object */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Tool input schema property
 * Supports nested object schemas and array<object> item schemas.
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpSchemaProperty
{
	GENERATED_BODY()

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString Description;

	UPROPERTY()
	bool bRequired = false;

	UPROPERTY()
	TArray<FString> Enum;

	/** Default value (optional) */
	TSharedPtr<FJsonValue> Default;

	/** Items type for array properties (e.g., "number" for array of numbers) */
	UPROPERTY()
	FString ItemsType;

	// ========== Nested Schema Support (P0 Uplift) ==========

	/** Nested object properties — used when Type == "object" */
	TMap<FString, TSharedPtr<FMcpSchemaProperty>> Properties;

	/** Required field names within this nested object */
	TArray<FString> NestedRequired;

	/** Items schema for array<object> — used when Type == "array" and items are objects */
	TSharedPtr<FMcpSchemaProperty> Items;

	/** Whether additional properties are allowed (JSON Schema additionalProperties) */
	UPROPERTY()
	bool bAdditionalProperties = false;

	/** Optional format hint (e.g., "uri", "date-time") */
	UPROPERTY()
	FString Format;

	/** Optional examples for documentation */
	TArray<TSharedPtr<FJsonValue>> Examples;

	/** Raw JSON Schema override — if set, merged directly into output, bypassing other fields */
	TSharedPtr<FJsonObject> RawSchema;

	TSharedPtr<FJsonObject> ToJson() const;

	/** 带递归深度保护的 ToJson 内部实现，防止循环引用导致栈溢出 */
	TSharedPtr<FJsonObject> ToJsonWithDepth(int32 CurrentDepth) const;

	// ========== Builder Helpers ==========

	/** Create a simple typed property */
	static FMcpSchemaProperty Make(const FString& InType, const FString& InDescription, bool bInRequired = false)
	{
		FMcpSchemaProperty Prop;
		Prop.Type = InType;
		Prop.Description = InDescription;
		Prop.bRequired = bInRequired;
		return Prop;
	}

	/** Create an enum property */
	static FMcpSchemaProperty MakeEnum(const FString& InDescription, const TArray<FString>& InEnum, bool bInRequired = false)
	{
		FMcpSchemaProperty Prop;
		Prop.Type = TEXT("string");
		Prop.Description = InDescription;
		Prop.bRequired = bInRequired;
		Prop.Enum = InEnum;
		return Prop;
	}

	/** Create an array property with simple item type */
	static FMcpSchemaProperty MakeArray(const FString& InDescription, const FString& InItemsType, bool bInRequired = false)
	{
		FMcpSchemaProperty Prop;
		Prop.Type = TEXT("array");
		Prop.Description = InDescription;
		Prop.bRequired = bInRequired;
		Prop.ItemsType = InItemsType;
		return Prop;
	}
};

/**
 * Tool definition for MCP tools/list response
 */
USTRUCT()
struct UEBRIDGEMCP_API FMcpToolDefinition
{
	GENERATED_BODY()

	/** Unique tool name */
	UPROPERTY()
	FString Name;

	/** Human-readable description */
	UPROPERTY()
	FString Description;

	/** Input schema properties */
	TMap<FString, FMcpSchemaProperty> InputSchema;

	/** Required property names */
	UPROPERTY()
	TArray<FString> Required;

	/** Tool kind: query, detail, batch, assert, utility */
	UPROPERTY()
	FString Kind = TEXT("query");

	/** Primary resource scope */
	UPROPERTY()
	FString ResourceScope = TEXT("generic");

	/** Whether the tool mutates editor state */
	UPROPERTY()
	bool bMutates = false;

	/** Whether the tool supports batched operations */
	UPROPERTY()
	bool bSupportsBatch = false;

	/** Whether the tool supports dry-run validation */
	UPROPERTY()
	bool bSupportsDryRun = false;

	/** Whether the tool can compile modified assets */
	UPROPERTY()
	bool bSupportsCompile = false;

	/** Whether the tool can save modified assets */
	UPROPERTY()
	bool bSupportsSave = false;

	TSharedPtr<FJsonObject> ToJson() const;
};
