// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Search/SearchProjectTool.h"

#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"
#include "Utils/McpAssetModifier.h"

namespace
{
	bool IncludesSection(const TSet<FString>& Sections, const FString& Section)
	{
		return Sections.Num() == 0 || Sections.Contains(Section);
	}

	TSet<FString> ReadSections(const TSharedPtr<FJsonObject>& Arguments)
	{
		TArray<FString> SectionValues;
		SearchToolUtils::ExtractStringArrayField(Arguments, TEXT("include"), SectionValues);
		TSet<FString> Sections;
		for (FString Section : SectionValues)
		{
			Section.ToLowerInline();
			if (Section == TEXT("asset"))
			{
				Section = TEXT("assets");
			}
			else if (Section == TEXT("blueprint") || Section == TEXT("symbols"))
			{
				Section = TEXT("blueprint_symbols");
			}
			else if (Section == TEXT("level") || Section == TEXT("actors"))
			{
				Section = TEXT("level_entities");
			}
			Sections.Add(Section);
		}
		return Sections;
	}

	void AddSectionField(TArray<SearchToolUtils::FSearchItem>& Items, const FString& Section)
	{
		for (SearchToolUtils::FSearchItem& Item : Items)
		{
			if (Item.Object.IsValid())
			{
				Item.Object->SetStringField(TEXT("section"), Section);
			}
		}
	}
}

FString USearchProjectTool::GetToolDescription() const
{
	return TEXT("Unified ranked search across assets, Blueprint symbols, and level entities.");
}

TMap<FString, FMcpSchemaProperty> USearchProjectTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Required search query applied across project search sections."), true));
	Schema.Add(TEXT("include"), FMcpSchemaProperty::MakeArray(TEXT("Optional sections: assets, blueprint_symbols, level_entities. Defaults to all."), TEXT("string")));
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional single package path prefix for asset and Blueprint searches. Defaults to /Game.")));
	Schema.Add(TEXT("paths"), FMcpSchemaProperty::MakeArray(TEXT("Optional package path prefixes for asset and Blueprint searches."), TEXT("string")));
	Schema.Add(TEXT("class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional asset class filter for the assets section.")));
	Schema.Add(TEXT("include_nodes"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include Blueprint graph nodes in symbol search. Default: false.")));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world for level_entities. Default: auto."), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("limit_per_section"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum results per section. Default: 25, max: 200.")));
	Schema.Add(TEXT("total_limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum flattened top results. Default: 50, max: 500.")));
	Schema.Add(TEXT("max_blueprints"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum Blueprints to load for symbol search. Default: 200, max: 1000.")));
	return Schema;
}

FMcpToolResult USearchProjectTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	if (Query.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_ARGUMENT"), TEXT("query is required"));
	}

	const TSet<FString> Sections = ReadSections(Arguments);
	const TArray<FString> Paths = SearchToolUtils::ReadPathFilters(Arguments);
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class"));
	const bool bIncludeNodes = GetBoolArgOrDefault(Arguments, TEXT("include_nodes"), false);
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const int32 LimitPerSection = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit_per_section"), 25), 1, 200);
	const int32 TotalLimit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("total_limit"), 50), 1, 500);
	const int32 MaxBlueprints = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("max_blueprints"), 200), 1, 1000);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);

	TArray<SearchToolUtils::FSearchItem> AllItems;
	TArray<TSharedPtr<FJsonValue>> Warnings;

	if (IncludesSection(Sections, TEXT("assets")))
	{
		TArray<SearchToolUtils::FSearchItem> AssetItems;
		int32 Scanned = 0;
		bool bClassFilterResolved = false;
		SearchToolUtils::CollectAssetResults(Query, Paths, ClassFilter, false, LimitPerSection, AssetItems, Scanned, bClassFilterResolved);
		AddSectionField(AssetItems, TEXT("assets"));
		AllItems.Append(AssetItems);

		TArray<TSharedPtr<FJsonValue>> AssetResults;
		SearchToolUtils::SortAndTrim(AssetItems, LimitPerSection, AssetResults);
		TSharedPtr<FJsonObject> SectionResult = MakeShareable(new FJsonObject);
		SectionResult->SetArrayField(TEXT("results"), AssetResults);
		SectionResult->SetNumberField(TEXT("count"), AssetResults.Num());
		SectionResult->SetNumberField(TEXT("total_matches"), AssetItems.Num());
		SectionResult->SetNumberField(TEXT("scanned"), Scanned);
		SectionResult->SetBoolField(TEXT("class_filter_resolved"), ClassFilter.IsEmpty() || bClassFilterResolved);
		Result->SetObjectField(TEXT("assets"), SectionResult);
	}

	if (IncludesSection(Sections, TEXT("blueprint_symbols")))
	{
		TArray<SearchToolUtils::FSearchItem> SymbolItems;
		TSet<FString> SymbolTypes;
		if (bIncludeNodes)
		{
			SymbolTypes.Add(TEXT("node"));
		}
		int32 BlueprintsScanned = 0;
		bool bBlueprintsTruncated = false;
		SearchToolUtils::CollectBlueprintSymbolResults(Query, Paths, SymbolTypes, bIncludeNodes, MaxBlueprints, LimitPerSection, SymbolItems, BlueprintsScanned, bBlueprintsTruncated);
		AddSectionField(SymbolItems, TEXT("blueprint_symbols"));
		AllItems.Append(SymbolItems);

		TArray<TSharedPtr<FJsonValue>> SymbolResults;
		SearchToolUtils::SortAndTrim(SymbolItems, LimitPerSection, SymbolResults);
		TSharedPtr<FJsonObject> SectionResult = MakeShareable(new FJsonObject);
		SectionResult->SetArrayField(TEXT("results"), SymbolResults);
		SectionResult->SetNumberField(TEXT("count"), SymbolResults.Num());
		SectionResult->SetNumberField(TEXT("total_matches"), SymbolItems.Num());
		SectionResult->SetNumberField(TEXT("blueprints_scanned"), BlueprintsScanned);
		SectionResult->SetBoolField(TEXT("blueprints_truncated"), bBlueprintsTruncated);
		Result->SetObjectField(TEXT("blueprint_symbols"), SectionResult);
	}

	if (IncludesSection(Sections, TEXT("level_entities")))
	{
		UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
		if (World)
		{
			TArray<SearchToolUtils::FSearchItem> EntityItems;
			int32 ActorsScanned = 0;
			SearchToolUtils::CollectLevelEntityResults(World, Query, FString(), FString(), FString(), false, true, false, LimitPerSection, Context, EntityItems, ActorsScanned);
			AddSectionField(EntityItems, TEXT("level_entities"));
			AllItems.Append(EntityItems);

			TArray<TSharedPtr<FJsonValue>> EntityResults;
			SearchToolUtils::SortAndTrim(EntityItems, LimitPerSection, EntityResults);
			TSharedPtr<FJsonObject> SectionResult = MakeShareable(new FJsonObject);
			SectionResult->SetStringField(TEXT("world_name"), World->GetName());
			SectionResult->SetStringField(TEXT("world_path"), World->GetPathName());
			SectionResult->SetArrayField(TEXT("results"), EntityResults);
			SectionResult->SetNumberField(TEXT("count"), EntityResults.Num());
			SectionResult->SetNumberField(TEXT("total_matches"), EntityItems.Num());
			SectionResult->SetNumberField(TEXT("actors_scanned"), ActorsScanned);
			Result->SetObjectField(TEXT("level_entities"), SectionResult);
		}
		else
		{
			TSharedPtr<FJsonObject> Warning = MakeShareable(new FJsonObject);
			Warning->SetStringField(TEXT("code"), TEXT("UEBMCP_WORLD_NOT_AVAILABLE"));
			Warning->SetStringField(TEXT("message"), TEXT("Skipped level_entities because no editor or PIE world is available"));
			Warnings.Add(MakeShareable(new FJsonValueObject(Warning)));
		}
	}

	TArray<TSharedPtr<FJsonValue>> TopResults;
	SearchToolUtils::SortAndTrim(AllItems, TotalLimit, TopResults);
	Result->SetArrayField(TEXT("results"), TopResults);
	Result->SetNumberField(TEXT("count"), TopResults.Num());
	Result->SetNumberField(TEXT("total_matches"), AllItems.Num());
	Result->SetBoolField(TEXT("truncated"), AllItems.Num() > TopResults.Num());
	Result->SetArrayField(TEXT("warnings"), Warnings);

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Project search complete"));
}
