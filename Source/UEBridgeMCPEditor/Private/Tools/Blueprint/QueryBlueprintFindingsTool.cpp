#include "Tools/Blueprint/QueryBlueprintFindingsTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Blueprint/BlueprintFindingUtils.h"
#include "Utils/McpV2ToolUtils.h"
#include "Engine/Blueprint.h"

FString UQueryBlueprintFindingsTool::GetToolDescription() const
{
	return TEXT("Return structural Blueprint findings such as orphan pins, unresolved references, missing defaults, and missing interface graphs.");
}

TMap<FString, FMcpSchemaProperty> UQueryBlueprintFindingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("graph_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional graph name filter")));
	Schema.Add(TEXT("graph_type"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional graph type filter")));
	Schema.Add(TEXT("max_findings"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum number of findings to return")));
	return Schema;
}

TArray<FString> UQueryBlueprintFindingsTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult UQueryBlueprintFindingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString GraphNameFilter = GetStringArgOrDefault(Arguments, TEXT("graph_name"));
	const FString GraphTypeFilter = GetStringArgOrDefault(Arguments, TEXT("graph_type"));
	const int32 MaxFindings = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("max_findings"), 100));

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	BlueprintFindingUtils::FQuery Query;
	Query.AssetPath = AssetPath;
	Query.SessionId = Context.SessionId;
	Query.GraphNameFilter = GraphNameFilter;
	Query.GraphTypeFilter = GraphTypeFilter;
	Query.MaxFindings = MaxFindings;

	TArray<TSharedPtr<FJsonValue>> Findings = BlueprintFindingUtils::CollectFindings(Blueprint, Query);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
	Result->SetArrayField(TEXT("findings"), Findings);
	Result->SetNumberField(TEXT("count"), Findings.Num());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Blueprint findings ready"));
}
