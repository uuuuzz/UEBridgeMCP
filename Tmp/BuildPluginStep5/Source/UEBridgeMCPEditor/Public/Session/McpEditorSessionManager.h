// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UWorld;
class AActor;
class UObject;

struct FMcpAssetHandle
{
	FString AssetPath;
	FString AssetClass;
};

struct FMcpEntityHandle
{
	FString Kind;
	FString SessionId;
	FString ResourcePath;
	FString EntityId;
	FString DisplayName;
};

struct FMcpEditorSessionCache
{
	FString SessionId;
	TMap<FString, TWeakObjectPtr<UObject>> AssetCache;
	TArray<FString> AssetCacheOrder;
	TMap<FString, TWeakObjectPtr<UEdGraphNode>> BlueprintNodeCache;
	TArray<FString> BlueprintNodeCacheOrder;
	TMap<FString, TWeakObjectPtr<AActor>> ActorCache;
	TArray<FString> ActorCacheOrder;
};

class UEBRIDGEMCPEDITOR_API FMcpEditorSessionManager
{
public:
	static FMcpEditorSessionManager& Get();

	FMcpEditorSessionCache& GetOrCreateSession(const FString& SessionId);
	void ResetSession(const FString& SessionId);
	void ResetAllSessions();

	UObject* ResolveAsset(const FString& SessionId, const FString& AssetPath, FString& OutError);

	template<typename T>
	T* ResolveAsset(const FString& SessionId, const FString& AssetPath, FString& OutError)
	{
		UObject* Object = ResolveAsset(SessionId, AssetPath, OutError);
		if (!Object)
		{
			return nullptr;
		}

		T* CastObject = Cast<T>(Object);
		if (!CastObject)
		{
			OutError = FString::Printf(TEXT("Asset '%s' is not of expected type %s"), *AssetPath, *T::StaticClass()->GetName());
			return nullptr;
		}
		return CastObject;
	}

	UEdGraphNode* ResolveBlueprintNode(
		const FString& SessionId,
		UBlueprint* Blueprint,
		const FGuid& NodeGuid,
		UEdGraph** OutGraph = nullptr);

	AActor* ResolveActor(
		const FString& SessionId,
		UWorld* World,
		const FString& ActorIdentifier,
		bool bTreatIdentifierAsObjectPath = false);

	void RememberBlueprintNode(
		const FString& SessionId,
		const FString& AssetPath,
		UEdGraphNode* Node);

	void RememberActor(
		const FString& SessionId,
		const FString& WorldIdentifier,
		AActor* Actor);

	static FString MakeBlueprintNodeCacheKey(const FString& AssetPath, const FGuid& NodeGuid);
	static FString MakeActorCacheKey(const FString& WorldIdentifier, const FString& ActorObjectPath);

private:
	FMcpEditorSessionManager() = default;

	FCriticalSection Lock;
	TMap<FString, FMcpEditorSessionCache> Sessions;
};
