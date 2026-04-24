// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/QueryBlueprintNodeTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

namespace
{
	bool TryReadNodeHandle(const TSharedPtr<FJsonObject>& Arguments, FString& OutAssetPath, FString& OutNodeGuid)
	{
		const TSharedPtr<FJsonObject>* HandleObject = nullptr;
		if (!Arguments->TryGetObjectField(TEXT("node_handle"), HandleObject) || !HandleObject || !(*HandleObject).IsValid())
		{
			return false;
		}

		(*HandleObject)->TryGetStringField(TEXT("resource_path"), OutAssetPath);
		(*HandleObject)->TryGetStringField(TEXT("entity_id"), OutNodeGuid);
		return !OutAssetPath.IsEmpty() && !OutNodeGuid.IsEmpty();
	}
}

FString UQueryBlueprintNodeTool::GetToolDescription() const
{
	return TEXT("Return detailed information for a specific Blueprint node, including object/class pin defaults.");
}

TMap<FString, FMcpSchemaProperty> UQueryBlueprintNodeTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path")));
	Schema.Add(TEXT("node_guid"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint node GUID")));
	Schema.Add(TEXT("node_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Node handle returned by query-blueprint-graph-summary")));
	Schema.Add(TEXT("include_pins"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include pin information")));
	Schema.Add(TEXT("include_connections"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include pin connections")));
	Schema.Add(TEXT("include_defaults"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include pin defaults")));
	Schema.Add(TEXT("include_position"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include node position")));
	return Schema;
}

TArray<FString> UQueryBlueprintNodeTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UQueryBlueprintNodeTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString NodeGuidString = GetStringArgOrDefault(Arguments, TEXT("node_guid"));
	if ((AssetPath.IsEmpty() || NodeGuidString.IsEmpty()) && !TryReadNodeHandle(Arguments, AssetPath, NodeGuidString))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' + 'node_guid' or 'node_handle' is required"));
	}

	const bool bIncludePins = GetBoolArgOrDefault(Arguments, TEXT("include_pins"), true);
	const bool bIncludeConnections = GetBoolArgOrDefault(Arguments, TEXT("include_connections"), true);
	const bool bIncludeDefaults = GetBoolArgOrDefault(Arguments, TEXT("include_defaults"), true);
	const bool bIncludePosition = GetBoolArgOrDefault(Arguments, TEXT("include_position"), true);

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidString, NodeGuid))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("Invalid node GUID format"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, NodeGuid, &Graph);
	if (!Node)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		Details->SetStringField(TEXT("node_guid"), NodeGuidString);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_NODE_NOT_FOUND"), TEXT("Blueprint node not found"), Details);
	}

	FMcpEditorSessionManager::Get().RememberBlueprintNode(Context.SessionId, AssetPath, Node);
	return FMcpToolResult::StructuredSuccess(
		McpV2ToolUtils::SerializeBlueprintNode(
			Node,
			AssetPath,
			Context.SessionId,
			Graph ? Graph->GetName() : TEXT(""),
			McpV2ToolUtils::GetBlueprintGraphType(Blueprint, Graph),
			bIncludePins,
			bIncludeConnections,
			bIncludeDefaults,
			bIncludePosition),
		TEXT("Blueprint node detail ready"));
}
