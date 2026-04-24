// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Session/McpEditorSessionManager.h"

#include "Utils/McpAssetModifier.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	constexpr int32 MaxCachedAssets = 128;
	constexpr int32 MaxCachedBlueprintNodes = 1024;
	constexpr int32 MaxCachedActors = 512;

	FString NormalizeSessionId(const FString& SessionId)
	{
		return SessionId.IsEmpty() ? TEXT("default") : SessionId;
	}

	void RemoveKeyFromOrder(TArray<FString>& Order, const FString& Key)
	{
		Order.RemoveSingle(Key);
	}

	template<typename TValue>
	void StoreCacheEntry(
		TMap<FString, TWeakObjectPtr<TValue>>& Cache,
		TArray<FString>& Order,
		const FString& Key,
		TValue* Value,
		int32 Limit)
	{
		if (!Value)
		{
			return;
		}

		Cache.Add(Key, Value);
		RemoveKeyFromOrder(Order, Key);
		Order.Add(Key);

		while (Cache.Num() > Limit && Order.Num() > 0)
		{
			const FString EvictedKey = Order[0];
			Order.RemoveAt(0);
			Cache.Remove(EvictedKey);
		}
	}

	AActor* FindActorByObjectPath(UWorld* World, const FString& ActorObjectPath)
	{
		if (!World || ActorObjectPath.IsEmpty())
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->GetPathName().Equals(ActorObjectPath, ESearchCase::IgnoreCase))
			{
				return Actor;
			}
		}

		return nullptr;
	}
}

FMcpEditorSessionManager& FMcpEditorSessionManager::Get()
{
	static FMcpEditorSessionManager Instance;
	return Instance;
}

FMcpEditorSessionCache& FMcpEditorSessionManager::GetOrCreateSession(const FString& SessionId)
{
	FScopeLock ScopeLock(&Lock);
	const FString EffectiveSessionId = NormalizeSessionId(SessionId);
	FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
	Session.SessionId = EffectiveSessionId;
	return Session;
}

void FMcpEditorSessionManager::ResetSession(const FString& SessionId)
{
	FScopeLock ScopeLock(&Lock);
	Sessions.Remove(NormalizeSessionId(SessionId));
}

void FMcpEditorSessionManager::ResetAllSessions()
{
	FScopeLock ScopeLock(&Lock);
	Sessions.Empty();
}

UObject* FMcpEditorSessionManager::ResolveAsset(const FString& SessionId, const FString& AssetPath, FString& OutError)
{
	const FString EffectiveSessionId = NormalizeSessionId(SessionId);
	{
		FScopeLock ScopeLock(&Lock);
		FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
		Session.SessionId = EffectiveSessionId;

		if (TWeakObjectPtr<UObject>* CachedObject = Session.AssetCache.Find(AssetPath))
		{
			if (CachedObject->IsValid())
			{
				return CachedObject->Get();
			}
			Session.AssetCache.Remove(AssetPath);
			RemoveKeyFromOrder(Session.AssetCacheOrder, AssetPath);
		}
	}

	UObject* LoadedObject = FMcpAssetModifier::LoadAssetByPath(AssetPath, OutError);
	if (!LoadedObject)
	{
		return nullptr;
	}

	{
		FScopeLock ScopeLock(&Lock);
		FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
		Session.SessionId = EffectiveSessionId;
		StoreCacheEntry(Session.AssetCache, Session.AssetCacheOrder, AssetPath, LoadedObject, MaxCachedAssets);
	}

	return LoadedObject;
}

UEdGraphNode* FMcpEditorSessionManager::ResolveBlueprintNode(
	const FString& SessionId,
	UBlueprint* Blueprint,
	const FGuid& NodeGuid,
	UEdGraph** OutGraph)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	const FString EffectiveSessionId = NormalizeSessionId(SessionId);
	const FString CacheKey = MakeBlueprintNodeCacheKey(Blueprint->GetPathName(), NodeGuid);
	{
		FScopeLock ScopeLock(&Lock);
		FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
		Session.SessionId = EffectiveSessionId;

		if (TWeakObjectPtr<UEdGraphNode>* CachedNode = Session.BlueprintNodeCache.Find(CacheKey))
		{
			if (CachedNode->IsValid())
			{
				UEdGraphNode* Node = CachedNode->Get();
				if (OutGraph)
				{
					*OutGraph = Node ? Node->GetGraph() : nullptr;
				}
				return Node;
			}

			Session.BlueprintNodeCache.Remove(CacheKey);
			RemoveKeyFromOrder(Session.BlueprintNodeCacheOrder, CacheKey);
		}
	}

	UEdGraph* FoundGraph = nullptr;
	UEdGraphNode* Node = FMcpAssetModifier::FindNodeByGuid(Blueprint, NodeGuid, &FoundGraph);
	if (!Node)
	{
		return nullptr;
	}

	{
		FScopeLock ScopeLock(&Lock);
		FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
		Session.SessionId = EffectiveSessionId;
		StoreCacheEntry(Session.BlueprintNodeCache, Session.BlueprintNodeCacheOrder, CacheKey, Node, MaxCachedBlueprintNodes);
	}

	if (OutGraph)
	{
		*OutGraph = FoundGraph;
	}
	return Node;
}

AActor* FMcpEditorSessionManager::ResolveActor(
	const FString& SessionId,
	UWorld* World,
	const FString& ActorIdentifier,
	bool bTreatIdentifierAsObjectPath)
{
	if (!World || ActorIdentifier.IsEmpty())
	{
		return nullptr;
	}

	const FString EffectiveSessionId = NormalizeSessionId(SessionId);
	const FString WorldIdentifier = World->GetPathName();
	if (bTreatIdentifierAsObjectPath)
	{
		const FString CacheKey = MakeActorCacheKey(WorldIdentifier, ActorIdentifier);
		{
			FScopeLock ScopeLock(&Lock);
			FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
			Session.SessionId = EffectiveSessionId;

			if (TWeakObjectPtr<AActor>* CachedActor = Session.ActorCache.Find(CacheKey))
			{
				if (CachedActor->IsValid())
				{
					return CachedActor->Get();
				}

				Session.ActorCache.Remove(CacheKey);
				RemoveKeyFromOrder(Session.ActorCacheOrder, CacheKey);
			}
		}

		AActor* Actor = FindActorByObjectPath(World, ActorIdentifier);
		if (!Actor)
		{
			return nullptr;
		}

		{
			FScopeLock ScopeLock(&Lock);
			FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
			Session.SessionId = EffectiveSessionId;
			StoreCacheEntry(Session.ActorCache, Session.ActorCacheOrder, CacheKey, Actor, MaxCachedActors);
		}
		return Actor;
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorIdentifier);
	if (Actor)
	{
		RememberActor(EffectiveSessionId, WorldIdentifier, Actor);
	}
	return Actor;
}

void FMcpEditorSessionManager::RememberBlueprintNode(
	const FString& SessionId,
	const FString& AssetPath,
	UEdGraphNode* Node)
{
	if (!Node)
	{
		return;
	}

	const FString EffectiveSessionId = NormalizeSessionId(SessionId);
	const FString CacheKey = MakeBlueprintNodeCacheKey(AssetPath, Node->NodeGuid);

	FScopeLock ScopeLock(&Lock);
	FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
	Session.SessionId = EffectiveSessionId;
	StoreCacheEntry(Session.BlueprintNodeCache, Session.BlueprintNodeCacheOrder, CacheKey, Node, MaxCachedBlueprintNodes);
}

void FMcpEditorSessionManager::RememberActor(
	const FString& SessionId,
	const FString& WorldIdentifier,
	AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	const FString EffectiveSessionId = NormalizeSessionId(SessionId);
	const FString CacheKey = MakeActorCacheKey(WorldIdentifier, Actor->GetPathName());

	FScopeLock ScopeLock(&Lock);
	FMcpEditorSessionCache& Session = Sessions.FindOrAdd(EffectiveSessionId);
	Session.SessionId = EffectiveSessionId;
	StoreCacheEntry(Session.ActorCache, Session.ActorCacheOrder, CacheKey, Actor, MaxCachedActors);
}

FString FMcpEditorSessionManager::MakeBlueprintNodeCacheKey(const FString& AssetPath, const FGuid& NodeGuid)
{
	return FString::Printf(TEXT("%s::%s"), *AssetPath, *NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
}

FString FMcpEditorSessionManager::MakeActorCacheKey(const FString& WorldIdentifier, const FString& ActorObjectPath)
{
	return FString::Printf(TEXT("%s::%s"), *WorldIdentifier, *ActorObjectPath);
}
