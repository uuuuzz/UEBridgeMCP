// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Search/SearchBlueprintSymbolsTool.h"

#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"

namespace
{
	TSet<FString> ReadSymbolTypes(const TSharedPtr<FJsonObject>& Arguments)
	{
		TArray<FString> TypeValues;
		SearchToolUtils::ExtractStringArrayField(Arguments, TEXT("symbol_types"), TypeValues);

		TSet<FString> Types;
		for (FString TypeValue : TypeValues)
		{
			TypeValue.ToLowerInline();
			if (TypeValue == TEXT("variables"))
			{
				TypeValue = TEXT("variable");
			}
			else if (TypeValue == TEXT("functions"))
			{
				TypeValue = TEXT("function");
			}
			else if (TypeValue == TEXT("macros"))
			{
				TypeValue = TEXT("macro");
			}
			else if (TypeValue == TEXT("graphs"))
			{
				TypeValue = TEXT("graph");
			}
			else if (TypeValue == TEXT("nodes"))
			{
				TypeValue = TEXT("node");
			}
			Types.Add(TypeValue);
		}
		return Types;
	}
}

FString USearchBlueprintSymbolsTool::GetToolDescription() const
{
	return TEXT("Search Blueprint variables, functions, macros, event graphs, and optionally graph nodes with ranked fuzzy matching.");
}

TMap<FString, FMcpSchemaProperty> USearchBlueprintSymbolsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional symbol query. Supports exact, contains, wildcard, camel-case, and fuzzy subsequence matches.")));
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional single package path prefix. Defaults to /Game.")));
	Schema.Add(TEXT("paths"), FMcpSchemaProperty::MakeArray(TEXT("Optional package path prefixes. Defaults to /Game."), TEXT("string")));
	Schema.Add(TEXT("symbol_types"), FMcpSchemaProperty::MakeArray(TEXT("Optional symbol types: variable, function, macro, graph, node."), TEXT("string")));
	Schema.Add(TEXT("include_nodes"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include graph nodes in addition to member/graph symbols. Default: false.")));
	Schema.Add(TEXT("max_blueprints"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum Blueprints to load while searching. Default: 200, max: 1000.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum ranked results to return. Default: 50, max: 500.")));
	return Schema;
}

FMcpToolResult USearchBlueprintSymbolsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	const TArray<FString> Paths = SearchToolUtils::ReadPathFilters(Arguments);
	TSet<FString> SymbolTypes = ReadSymbolTypes(Arguments);
	const bool bIncludeNodes = GetBoolArgOrDefault(Arguments, TEXT("include_nodes"), false);
	if (bIncludeNodes)
	{
		SymbolTypes.Add(TEXT("node"));
	}
	const int32 MaxBlueprints = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("max_blueprints"), 200), 1, 1000);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 50), 1, 500);

	TArray<SearchToolUtils::FSearchItem> Items;
	int32 BlueprintsScanned = 0;
	bool bBlueprintsTruncated = false;
	SearchToolUtils::CollectBlueprintSymbolResults(Query, Paths, SymbolTypes, bIncludeNodes, MaxBlueprints, Limit, Items, BlueprintsScanned, bBlueprintsTruncated);

	TArray<TSharedPtr<FJsonValue>> Results;
	SearchToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetNumberField(TEXT("blueprints_scanned"), BlueprintsScanned);
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());
	Result->SetBoolField(TEXT("blueprints_truncated"), bBlueprintsTruncated);
	Result->SetBoolField(TEXT("include_nodes"), bIncludeNodes);

	TArray<TSharedPtr<FJsonValue>> TypeArray;
	for (const FString& SymbolType : SymbolTypes)
	{
		TypeArray.Add(MakeShareable(new FJsonValueString(SymbolType)));
	}
	Result->SetArrayField(TEXT("symbol_types"), TypeArray);

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Blueprint symbol search complete"));
}
