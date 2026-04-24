// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Dom/JsonObject.h"
#include "Tools/McpToolBase.h"

class UWorld;
class AActor;
struct FHitResult;

struct FLevelResolvedActorHandle
{
	FString SessionId;
	FString ResourcePath;
	FString EntityId;
	FString DisplayName;

	bool IsUsable() const
	{
		return !EntityId.IsEmpty() || !DisplayName.IsEmpty();
	}
};

struct FLevelActorReference
{
	FString ActorName;
	FLevelResolvedActorHandle Handle;

	bool HasAny() const
	{
		return !ActorName.IsEmpty() || Handle.IsUsable();
	}
};

namespace LevelActorToolUtils
{
	bool TryReadActorHandle(
		const TSharedPtr<FJsonObject>& Object,
		const FString& FieldName,
		FLevelResolvedActorHandle& OutHandle);

	bool ReadActorReference(
		const TSharedPtr<FJsonObject>& Object,
		const FString& ActorNameField,
		const FString& ActorHandleField,
		FLevelActorReference& OutReference);

	UWorld* ResolveWorldForHandle(const FString& RequestedWorldType, const FString& ResourcePath);

	AActor* ResolveActorReference(
		const TSharedPtr<FJsonObject>& Object,
		const FString& RequestedWorldType,
		const FString& ActorNameField,
		const FString& ActorHandleField,
		const FMcpToolContext& Context,
		UWorld*& OutWorld,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		TSharedPtr<FJsonObject>& OutDetails,
		bool bReferenceRequired = true);

	bool ResolveActorReferences(
		const TArray<TSharedPtr<FJsonValue>>& References,
		const FString& RequestedWorldType,
		const FMcpToolContext& Context,
		UWorld*& OutWorld,
		TArray<AActor*>& OutActors,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		TSharedPtr<FJsonObject>& OutDetails);

	FString MobilityToString(EComponentMobility::Type Mobility);
	bool TryParseMobility(const FString& Value, EComponentMobility::Type& OutMobility);

	bool TraceGroundBelowActor(
		UWorld* World,
		AActor* Actor,
		double TraceDistance,
		FHitResult& OutHit,
		FVector* OutBoundsOrigin = nullptr,
		FVector* OutBoundsExtent = nullptr);

	void AppendWorldModifiedAsset(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets);

	bool SaveWorldIfNeeded(
		UWorld* World,
		bool bSave,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets,
		FString& OutErrorCode,
		FString& OutErrorMessage);
}
