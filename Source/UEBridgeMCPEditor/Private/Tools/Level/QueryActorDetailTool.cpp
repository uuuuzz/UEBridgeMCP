// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryActorDetailTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

namespace
{
	struct FResolvedActorHandle
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

	bool TryReadActorHandle(const TSharedPtr<FJsonObject>& Arguments, FResolvedActorHandle& OutHandle)
	{
		const TSharedPtr<FJsonObject>* HandleObject = nullptr;
		if (!Arguments->TryGetObjectField(TEXT("actor_handle"), HandleObject) || !HandleObject || !(*HandleObject).IsValid())
		{
			return false;
		}

		(*HandleObject)->TryGetStringField(TEXT("session_id"), OutHandle.SessionId);
		(*HandleObject)->TryGetStringField(TEXT("resource_path"), OutHandle.ResourcePath);
		(*HandleObject)->TryGetStringField(TEXT("entity_id"), OutHandle.EntityId);
		(*HandleObject)->TryGetStringField(TEXT("display_name"), OutHandle.DisplayName);
		return OutHandle.IsUsable();
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
}

FString UQueryActorDetailTool::GetToolDescription() const
{
	return TEXT("Return detailed reflected information for a specific actor in the editor or PIE world.");
}

TMap<FString, FMcpSchemaProperty> UQueryActorDetailTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), {TEXT("editor"), TEXT("pie"), TEXT("auto")}));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle returned by query-level-summary")));
	Schema.Add(TEXT("include_components"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include components")));
	Schema.Add(TEXT("include_properties"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include reflected properties")));
	Schema.Add(TEXT("include_inherited"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include inherited properties")));
	return Schema;
}

TArray<FString> UQueryActorDetailTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UQueryActorDetailTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), true);
	const bool bIncludeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_properties"), false);
	const bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), false);

	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	FResolvedActorHandle ActorHandle;
	const bool bHasHandle = TryReadActorHandle(Arguments, ActorHandle);
	if (ActorName.IsEmpty() && !bHasHandle)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actor_name' or 'actor_handle' is required"));
	}

	if (bHasHandle && !ActorHandle.SessionId.IsEmpty() && ActorHandle.SessionId != Context.SessionId)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("handle_session_id"), ActorHandle.SessionId);
		Details->SetStringField(TEXT("request_session_id"), Context.SessionId);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_HANDLE_SESSION_MISMATCH"), TEXT("Actor handle was created for a different MCP session"), Details);
	}

	UWorld* World = bHasHandle ? ResolveWorldForHandle(WorldType, ActorHandle.ResourcePath) : FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		if (bHasHandle && !ActorHandle.ResourcePath.IsEmpty())
		{
			Details->SetStringField(TEXT("resource_path"), ActorHandle.ResourcePath);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world matching the actor handle is available"), Details);
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	AActor* Actor = nullptr;
	if (bHasHandle && !ActorHandle.EntityId.IsEmpty())
	{
		Actor = FMcpEditorSessionManager::Get().ResolveActor(Context.SessionId, World, ActorHandle.EntityId, true);
	}

	if (!Actor)
	{
		const FString FallbackName = !ActorName.IsEmpty() ? ActorName : ActorHandle.DisplayName;
		if (!FallbackName.IsEmpty())
		{
			Actor = FMcpEditorSessionManager::Get().ResolveActor(Context.SessionId, World, FallbackName, false);
		}
	}

	if (!Actor)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		if (bHasHandle)
		{
			Details->SetStringField(TEXT("resource_path"), ActorHandle.ResourcePath);
			Details->SetStringField(TEXT("entity_id"), ActorHandle.EntityId);
			Details->SetStringField(TEXT("display_name"), ActorHandle.DisplayName);
		}
		if (!ActorName.IsEmpty())
		{
			Details->SetStringField(TEXT("actor_name"), ActorName);
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), TEXT("Actor not found"), Details);
	}

	FMcpEditorSessionManager::Get().RememberActor(Context.SessionId, World->GetPathName(), Actor);
	return FMcpToolResult::StructuredSuccess(
		McpV2ToolUtils::SerializeActorDetail(Actor, Context.SessionId, bIncludeComponents, bIncludeProperties, bIncludeInherited),
		TEXT("Actor detail ready"));
}
