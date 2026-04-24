#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;

namespace BlueprintFindingUtils
{
	struct FQuery
	{
		FString AssetPath;
		FString SessionId;
		FString GraphNameFilter;
		FString GraphTypeFilter;
		int32 MaxFindings = 100;
	};

	TArray<TSharedPtr<FJsonValue>> CollectFindings(UBlueprint* Blueprint, const FQuery& Query);
}
