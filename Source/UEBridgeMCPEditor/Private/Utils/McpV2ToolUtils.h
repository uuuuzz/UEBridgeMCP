// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UWorld;
class AActor;
class UActorComponent;

namespace McpV2ToolUtils
{
	TSharedPtr<FJsonObject> MakeAssetHandle(const FString& AssetPath, const FString& AssetClass);
	TSharedPtr<FJsonObject> MakeEntityHandle(const FString& Kind, const FString& SessionId, const FString& ResourcePath, const FString& EntityId, const FString& DisplayName);

	FString GetBlueprintGraphType(const UBlueprint* Blueprint, const UEdGraph* Graph);

	TSharedPtr<FJsonObject> BuildBlueprintSummary(UBlueprint* Blueprint, const FString& AssetPath, bool bIncludeNames);
	TSharedPtr<FJsonObject> BuildBlueprintGraphSummary(
		UBlueprint* Blueprint,
		const FString& AssetPath,
		const FString& SessionId,
		const FString& GraphNameFilter,
		const FString& GraphTypeFilter,
		bool bIncludeSampleNodes,
		int32 MaxSampleNodes);

	TSharedPtr<FJsonObject> SerializeBlueprintNode(
		UEdGraphNode* Node,
		const FString& AssetPath,
		const FString& SessionId,
		const FString& GraphName,
		const FString& GraphType,
		bool bIncludePins,
		bool bIncludeConnections,
		bool bIncludeDefaults,
		bool bIncludePosition);

	bool MatchesPattern(const FString& Value, const FString& Filter);
	TSharedPtr<FJsonObject> BuildWorldSummary(UWorld* World, const FString& SessionId, bool bIncludeLevels, bool bIncludeSelection);
	TSharedPtr<FJsonObject> SerializeActorSummary(AActor* Actor, const FString& SessionId, bool bIncludeTransform);
	TSharedPtr<FJsonObject> SerializeActorDetail(AActor* Actor, const FString& SessionId, bool bIncludeComponents, bool bIncludeProperties, bool bIncludeInherited);
}
