// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Search/SearchAssetsAdvancedTool.h"

#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"

FString USearchAssetsAdvancedTool::GetToolDescription() const
{
	return TEXT("Search project assets with ranked fuzzy/camel-case matching, optional path and class filters, and stable result limiting.");
}

TMap<FString, FMcpSchemaProperty> USearchAssetsAdvancedTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional name/path/class query. Supports exact, contains, wildcard, camel-case, and fuzzy subsequence matches.")));
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional single package path prefix, e.g. /Game/Characters.")));
	Schema.Add(TEXT("paths"), FMcpSchemaProperty::MakeArray(TEXT("Optional package path prefixes. Defaults to /Game."), TEXT("string")));
	Schema.Add(TEXT("class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional asset class filter, e.g. Blueprint, Material, StaticMesh.")));
	Schema.Add(TEXT("include_only_on_disk_assets"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Only include assets that are on disk. Default: false.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum ranked results to return. Default: 50, max: 500.")));
	return Schema;
}

FMcpToolResult USearchAssetsAdvancedTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	const TArray<FString> Paths = SearchToolUtils::ReadPathFilters(Arguments);
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class"));
	const bool bIncludeOnlyOnDiskAssets = GetBoolArgOrDefault(Arguments, TEXT("include_only_on_disk_assets"), false);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 50), 1, 500);

	TArray<SearchToolUtils::FSearchItem> Items;
	int32 Scanned = 0;
	bool bClassFilterResolved = false;
	SearchToolUtils::CollectAssetResults(Query, Paths, ClassFilter, bIncludeOnlyOnDiskAssets, Limit, Items, Scanned, bClassFilterResolved);

	TArray<TSharedPtr<FJsonValue>> Results;
	SearchToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetNumberField(TEXT("scanned"), Scanned);
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());
	Result->SetBoolField(TEXT("class_filter_resolved"), ClassFilter.IsEmpty() || bClassFilterResolved);
	if (!ClassFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("class"), ClassFilter);
	}
	TArray<TSharedPtr<FJsonValue>> PathArray;
	for (const FString& Path : Paths)
	{
		PathArray.Add(MakeShareable(new FJsonValueString(Path)));
	}
	Result->SetArrayField(TEXT("paths"), PathArray);

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Asset search complete"));
}
