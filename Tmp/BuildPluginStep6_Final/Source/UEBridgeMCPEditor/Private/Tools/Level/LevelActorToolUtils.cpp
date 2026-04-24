// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/LevelActorToolUtils.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpAssetModifier.h"
#include "CollisionQueryParams.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"

namespace
{
	TSharedPtr<FJsonObject> MakeReferenceErrorDetails(
		const FString& ActorNameField,
		const FString& ActorHandleField,
		const FLevelActorReference& Reference)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		if (!Reference.ActorName.IsEmpty())
		{
			Details->SetStringField(ActorNameField, Reference.ActorName);
		}
		if (Reference.Handle.IsUsable())
		{
			TSharedPtr<FJsonObject> HandleObject = MakeShareable(new FJsonObject);
			if (!Reference.Handle.SessionId.IsEmpty())
			{
				HandleObject->SetStringField(TEXT("session_id"), Reference.Handle.SessionId);
			}
			if (!Reference.Handle.ResourcePath.IsEmpty())
			{
				HandleObject->SetStringField(TEXT("resource_path"), Reference.Handle.ResourcePath);
			}
			if (!Reference.Handle.EntityId.IsEmpty())
			{
				HandleObject->SetStringField(TEXT("entity_id"), Reference.Handle.EntityId);
			}
			if (!Reference.Handle.DisplayName.IsEmpty())
			{
				HandleObject->SetStringField(TEXT("display_name"), Reference.Handle.DisplayName);
			}
			Details->SetObjectField(ActorHandleField, HandleObject);
		}
		return Details;
	}
}

namespace LevelActorToolUtils
{
	bool TryReadActorHandle(
		const TSharedPtr<FJsonObject>& Object,
		const FString& FieldName,
		FLevelResolvedActorHandle& OutHandle)
	{
		const TSharedPtr<FJsonObject>* HandleObject = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(FieldName, HandleObject) || !HandleObject || !(*HandleObject).IsValid())
		{
			return false;
		}

		(*HandleObject)->TryGetStringField(TEXT("session_id"), OutHandle.SessionId);
		(*HandleObject)->TryGetStringField(TEXT("resource_path"), OutHandle.ResourcePath);
		(*HandleObject)->TryGetStringField(TEXT("entity_id"), OutHandle.EntityId);
		(*HandleObject)->TryGetStringField(TEXT("display_name"), OutHandle.DisplayName);
		return OutHandle.IsUsable();
	}

	bool ReadActorReference(
		const TSharedPtr<FJsonObject>& Object,
		const FString& ActorNameField,
		const FString& ActorHandleField,
		FLevelActorReference& OutReference)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		Object->TryGetStringField(ActorNameField, OutReference.ActorName);
		TryReadActorHandle(Object, ActorHandleField, OutReference.Handle);
		return OutReference.HasAny();
	}

	UWorld* ResolveWorldForHandle(const FString& RequestedWorldType, const FString& ResourcePath)
	{
		if (ResourcePath.IsEmpty())
		{
			return FMcpAssetModifier::ResolveWorld(RequestedWorldType);
		}

		if (UWorld* RequestedWorld = FMcpAssetModifier::ResolveWorld(RequestedWorldType))
		{
			if (RequestedWorld->GetPathName().Equals(ResourcePath, ESearchCase::IgnoreCase))
			{
				return RequestedWorld;
			}
		}

		if (UWorld* EditorWorld = FMcpAssetModifier::ResolveWorld(TEXT("editor")))
		{
			if (EditorWorld->GetPathName().Equals(ResourcePath, ESearchCase::IgnoreCase))
			{
				return EditorWorld;
			}
		}

		if (UWorld* PieWorld = FMcpAssetModifier::ResolveWorld(TEXT("pie")))
		{
			if (PieWorld->GetPathName().Equals(ResourcePath, ESearchCase::IgnoreCase))
			{
				return PieWorld;
			}
		}

		return nullptr;
	}

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
		bool bReferenceRequired)
	{
		FLevelActorReference Reference;
		const bool bHasReference = ReadActorReference(Object, ActorNameField, ActorHandleField, Reference);
		if (!bHasReference)
		{
			if (!bReferenceRequired)
			{
				OutWorld = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
				return nullptr;
			}

			OutErrorCode = TEXT("UEBMCP_MISSING_REQUIRED_FIELD");
			OutErrorMessage = FString::Printf(TEXT("'%s' or '%s' is required"), *ActorNameField, *ActorHandleField);
			return nullptr;
		}

		if (Reference.Handle.IsUsable() &&
			!Reference.Handle.SessionId.IsEmpty() &&
			Reference.Handle.SessionId != Context.SessionId)
		{
			OutErrorCode = TEXT("UEBMCP_HANDLE_SESSION_MISMATCH");
			OutErrorMessage = TEXT("Actor handle was created for a different MCP session");
			OutDetails = MakeReferenceErrorDetails(ActorNameField, ActorHandleField, Reference);
			OutDetails->SetStringField(TEXT("handle_session_id"), Reference.Handle.SessionId);
			OutDetails->SetStringField(TEXT("request_session_id"), Context.SessionId);
			return nullptr;
		}

		UWorld* World = Reference.Handle.IsUsable()
			? ResolveWorldForHandle(RequestedWorldType, Reference.Handle.ResourcePath)
			: FMcpAssetModifier::ResolveWorld(RequestedWorldType);
		if (!World)
		{
			OutErrorCode = TEXT("UEBMCP_WORLD_NOT_AVAILABLE");
			OutErrorMessage = Reference.Handle.IsUsable() && !Reference.Handle.ResourcePath.IsEmpty()
				? TEXT("No world matching the actor handle is available")
				: TEXT("No world available");
			OutDetails = MakeReferenceErrorDetails(ActorNameField, ActorHandleField, Reference);
			return nullptr;
		}

		AActor* Actor = nullptr;
		if (Reference.Handle.IsUsable() && !Reference.Handle.EntityId.IsEmpty())
		{
			Actor = FMcpEditorSessionManager::Get().ResolveActor(Context.SessionId, World, Reference.Handle.EntityId, true);
		}

		if (!Actor)
		{
			const FString LookupName = !Reference.ActorName.IsEmpty()
				? Reference.ActorName
				: Reference.Handle.DisplayName;
			if (!LookupName.IsEmpty())
			{
				Actor = FMcpEditorSessionManager::Get().ResolveActor(Context.SessionId, World, LookupName, false);
			}
		}

		if (!Actor)
		{
			OutErrorCode = TEXT("UEBMCP_ACTOR_NOT_FOUND");
			OutErrorMessage = TEXT("Actor not found");
			OutDetails = MakeReferenceErrorDetails(ActorNameField, ActorHandleField, Reference);
			return nullptr;
		}

		FMcpEditorSessionManager::Get().RememberActor(Context.SessionId, World->GetPathName(), Actor);
		OutWorld = World;
		return Actor;
	}

	bool ResolveActorReferences(
		const TArray<TSharedPtr<FJsonValue>>& References,
		const FString& RequestedWorldType,
		const FMcpToolContext& Context,
		UWorld*& OutWorld,
		TArray<AActor*>& OutActors,
		FString& OutErrorCode,
		FString& OutErrorMessage,
		TSharedPtr<FJsonObject>& OutDetails)
	{
		OutWorld = nullptr;
		OutActors.Reset();

		for (int32 Index = 0; Index < References.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& ReferenceValue = References[Index];
			const TSharedPtr<FJsonObject>* ReferenceObject = nullptr;
			if (!ReferenceValue.IsValid() || !ReferenceValue->TryGetObject(ReferenceObject) || !ReferenceObject || !(*ReferenceObject).IsValid())
			{
				OutErrorCode = TEXT("UEBMCP_INVALID_ARG");
				OutErrorMessage = FString::Printf(TEXT("actors[%d] must be an object"), Index);
				OutDetails = MakeShareable(new FJsonObject);
				OutDetails->SetNumberField(TEXT("index"), Index);
				return false;
			}

			UWorld* ResolvedWorld = nullptr;
			AActor* Actor = ResolveActorReference(
				*ReferenceObject,
				RequestedWorldType,
				TEXT("actor_name"),
				TEXT("actor_handle"),
				Context,
				ResolvedWorld,
				OutErrorCode,
				OutErrorMessage,
				OutDetails,
				true);
			if (!Actor)
			{
				if (!OutDetails.IsValid())
				{
					OutDetails = MakeShareable(new FJsonObject);
				}
				OutDetails->SetNumberField(TEXT("index"), Index);
				return false;
			}

			if (!OutWorld)
			{
				OutWorld = ResolvedWorld;
			}
			else if (ResolvedWorld && OutWorld->GetPathName() != ResolvedWorld->GetPathName())
			{
				OutErrorCode = TEXT("UEBMCP_WORLD_MISMATCH");
				OutErrorMessage = TEXT("All actor references must resolve to the same world");
				OutDetails = MakeShareable(new FJsonObject);
				OutDetails->SetNumberField(TEXT("index"), Index);
				OutDetails->SetStringField(TEXT("expected_world"), OutWorld->GetPathName());
				OutDetails->SetStringField(TEXT("actual_world"), ResolvedWorld->GetPathName());
				return false;
			}

			OutActors.Add(Actor);
		}

		return true;
	}

	FString MobilityToString(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
		case EComponentMobility::Static:
			return TEXT("Static");
		case EComponentMobility::Stationary:
			return TEXT("Stationary");
		case EComponentMobility::Movable:
			return TEXT("Movable");
		default:
			return TEXT("Unknown");
		}
	}

	bool TryParseMobility(const FString& Value, EComponentMobility::Type& OutMobility)
	{
		if (Value.Equals(TEXT("Static"), ESearchCase::IgnoreCase))
		{
			OutMobility = EComponentMobility::Static;
			return true;
		}
		if (Value.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase))
		{
			OutMobility = EComponentMobility::Stationary;
			return true;
		}
		if (Value.Equals(TEXT("Movable"), ESearchCase::IgnoreCase))
		{
			OutMobility = EComponentMobility::Movable;
			return true;
		}
		return false;
	}

	bool TraceGroundBelowActor(
		UWorld* World,
		AActor* Actor,
		double TraceDistance,
		FHitResult& OutHit,
		FVector* OutBoundsOrigin,
		FVector* OutBoundsExtent)
	{
		if (!World || !Actor)
		{
			return false;
		}

		FVector BoundsOrigin = FVector::ZeroVector;
		FVector BoundsExtent = FVector::ZeroVector;
		Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent, false);

		if (OutBoundsOrigin)
		{
			*OutBoundsOrigin = BoundsOrigin;
		}
		if (OutBoundsExtent)
		{
			*OutBoundsExtent = BoundsExtent;
		}

		const FVector TraceStart = BoundsOrigin + FVector(0.0, 0.0, BoundsExtent.Z + 10.0);
		const FVector TraceEnd = BoundsOrigin - FVector(0.0, 0.0, TraceDistance);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UEBridgeMCPTraceGroundBelowActor), true, Actor);
		return World->LineTraceSingleByChannel(OutHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	}

	void AppendWorldModifiedAsset(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		if (!World || !World->PersistentLevel)
		{
			return;
		}

		const FString LevelPath = World->PersistentLevel->GetPackage()
			? World->PersistentLevel->GetPackage()->GetPathName()
			: FString();
		if (!LevelPath.IsEmpty())
		{
			OutModifiedAssets.Add(MakeShareable(new FJsonValueString(LevelPath)));
		}
	}

	bool SaveWorldIfNeeded(
		UWorld* World,
		bool bSave,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets,
		FString& OutErrorCode,
		FString& OutErrorMessage)
	{
		AppendWorldModifiedAsset(World, OutModifiedAssets);

		if (!bSave)
		{
			return true;
		}

		if (!World || World->WorldType != EWorldType::Editor || !World->PersistentLevel)
		{
			OutWarnings.Add(MakeShareable(new FJsonValueString(
				TEXT("save=true was ignored because the resolved world is not a saveable editor world"))));
			return true;
		}

		if (!FEditorFileUtils::SaveLevel(World->PersistentLevel))
		{
			OutErrorCode = TEXT("UEBMCP_LEVEL_SAVE_FAILED");
			OutErrorMessage = TEXT("Failed to save the current editor level");
			return false;
		}

		return true;
	}
}
