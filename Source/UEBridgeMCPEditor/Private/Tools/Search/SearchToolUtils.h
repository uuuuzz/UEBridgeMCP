// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"

class UWorld;
struct FMcpToolContext;

namespace SearchToolUtils
{
	struct FSearchItem
	{
		double Score = 0.0;
		TSharedPtr<FJsonObject> Object;
	};

	FString NormalizeToken(FString Value);
	FString BuildCamelCaseAcronym(const FString& Value);
	double ScoreText(const FString& Value, const FString& Query);
	double ScoreFields(const TArray<FString>& Fields, const FString& Query);
	bool MatchesQuery(const TArray<FString>& Fields, const FString& Query);
	void SortAndTrim(TArray<FSearchItem>& Items, int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutArray);
	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues);
	TArray<FString> ReadPathFilters(const TSharedPtr<FJsonObject>& Arguments, const FString& DefaultPath = TEXT("/Game"));
	bool PathMatchesAny(const FString& Value, const TArray<FString>& PathFilters);

	TSharedPtr<FJsonObject> SerializeAssetData(const FAssetData& AssetData, const FString& Query, double Score, const FString& Kind = TEXT("asset"));

	void CollectAssetResults(
		const FString& Query,
		const TArray<FString>& Paths,
		const FString& ClassFilter,
		bool bIncludeOnlyOnDiskAssets,
		int32 Limit,
		TArray<FSearchItem>& OutItems,
		int32& OutScanned,
		bool& bOutClassFilterResolved);

	void CollectBlueprintSymbolResults(
		const FString& Query,
		const TArray<FString>& Paths,
		const TSet<FString>& SymbolTypes,
		bool bIncludeNodes,
		int32 MaxBlueprints,
		int32 Limit,
		TArray<FSearchItem>& OutItems,
		int32& OutBlueprintsScanned,
		bool& bOutBlueprintsTruncated);

	void CollectLevelEntityResults(
		UWorld* World,
		const FString& Query,
		const FString& ClassFilter,
		const FString& FolderFilter,
		const FString& TagFilter,
		bool bIncludeHidden,
		bool bIncludeTransform,
		bool bIncludeBounds,
		int32 Limit,
		const FMcpToolContext& Context,
		TArray<FSearchItem>& OutItems,
		int32& OutActorsScanned);
}
