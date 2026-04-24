// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Search/SearchLevelEntitiesTool.h"

#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"
#include "Utils/McpAssetModifier.h"

FString USearchLevelEntitiesTool::GetToolDescription() const
{
	return TEXT("Search actors/entities in the editor or PIE world with ranked label/class/folder/tag matching.");
}

TMap<FString, FMcpSchemaProperty> USearchLevelEntitiesTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor label/name/class/folder/tag query.")));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world. Default: auto."), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("class_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor class filter.")));
	Schema.Add(TEXT("folder_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional World Outliner folder filter.")));
	Schema.Add(TEXT("tag_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor tag filter.")));
	Schema.Add(TEXT("include_hidden"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include hidden editor actors. Default: false.")));
	Schema.Add(TEXT("include_transform"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include transforms. Default: true.")));
	Schema.Add(TEXT("include_bounds"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actor bounds. Default: false.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum ranked results to return. Default: 50, max: 500.")));
	return Schema;
}

FMcpToolResult USearchLevelEntitiesTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"));
	const FString FolderFilter = GetStringArgOrDefault(Arguments, TEXT("folder_filter"));
	const FString TagFilter = GetStringArgOrDefault(Arguments, TEXT("tag_filter"));
	const bool bIncludeHidden = GetBoolArgOrDefault(Arguments, TEXT("include_hidden"), false);
	const bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
	const bool bIncludeBounds = GetBoolArgOrDefault(Arguments, TEXT("include_bounds"), false);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 50), 1, 500);

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No editor or PIE world is available"));
	}

	TArray<SearchToolUtils::FSearchItem> Items;
	int32 ActorsScanned = 0;
	SearchToolUtils::CollectLevelEntityResults(World, Query, ClassFilter, FolderFilter, TagFilter, bIncludeHidden, bIncludeTransform, bIncludeBounds, Limit, Context, Items, ActorsScanned);

	TArray<TSharedPtr<FJsonValue>> Results;
	SearchToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("world_path"), World->GetPathName());
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetNumberField(TEXT("actors_scanned"), ActorsScanned);
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Level entity search complete"));
}
