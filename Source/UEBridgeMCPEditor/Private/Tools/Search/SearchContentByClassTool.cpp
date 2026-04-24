// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Search/SearchContentByClassTool.h"

#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"

FString USearchContentByClassTool::GetToolDescription() const
{
	return TEXT("Find content assets by class with optional ranked name/path filtering.");
}

TMap<FString, FMcpSchemaProperty> USearchContentByClassTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Required asset class filter, e.g. Blueprint, Material, Texture2D, StaticMesh."), true));
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional ranked query applied to asset name, path, package, and class.")));
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional single package path prefix, e.g. /Game/Validation.")));
	Schema.Add(TEXT("paths"), FMcpSchemaProperty::MakeArray(TEXT("Optional package path prefixes. Defaults to /Game."), TEXT("string")));
	Schema.Add(TEXT("include_only_on_disk_assets"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Only include assets that are on disk. Default: false.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum ranked results to return. Default: 50, max: 500.")));
	return Schema;
}

FMcpToolResult USearchContentByClassTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class"));
	if (ClassFilter.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_ARGUMENT"), TEXT("class is required"));
	}

	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	const TArray<FString> Paths = SearchToolUtils::ReadPathFilters(Arguments);
	const bool bIncludeOnlyOnDiskAssets = GetBoolArgOrDefault(Arguments, TEXT("include_only_on_disk_assets"), false);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 50), 1, 500);

	TArray<SearchToolUtils::FSearchItem> Items;
	int32 Scanned = 0;
	bool bClassFilterResolved = false;
	SearchToolUtils::CollectAssetResults(Query, Paths, ClassFilter, bIncludeOnlyOnDiskAssets, Limit, Items, Scanned, bClassFilterResolved);

	TArray<TSharedPtr<FJsonValue>> Results;
	SearchToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("class"), ClassFilter);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetNumberField(TEXT("scanned"), Scanned);
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());
	Result->SetBoolField(TEXT("class_filter_resolved"), bClassFilterResolved);
	if (!bClassFilterResolved)
	{
		Result->SetStringField(TEXT("warning"), TEXT("Class could not be resolved directly; results used asset class name/path fallback matching."));
	}

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Content class search complete"));
}
