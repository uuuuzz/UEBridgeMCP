// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Physics/QueryPhysicsSummaryTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

FString UQueryPhysicsSummaryTool::GetToolDescription() const
{
	return TEXT("Return physics and collision summaries for a world or a specific actor, including primitive components and physics constraints.");
}

TMap<FString, FMcpSchemaProperty> UQueryPhysicsSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional actor handle returned by query-level-summary")));
	Schema.Add(TEXT("include_components"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include PrimitiveComponent details for actor summaries")));
	Schema.Add(TEXT("include_constraints"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include PhysicsConstraintComponent details for actor summaries")));
	return Schema;
}

FMcpToolResult UQueryPhysicsSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), true);
	const bool bIncludeConstraints = GetBoolArgOrDefault(Arguments, TEXT("include_constraints"), true);

	UWorld* World = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	AActor* Actor = LevelActorToolUtils::ResolveActorReference(
		Arguments,
		WorldType,
		TEXT("actor_name"),
		TEXT("actor_handle"),
		Context,
		World,
		ErrorCode,
		ErrorMessage,
		ErrorDetails,
		false);
	if (!Actor && (Arguments->HasField(TEXT("actor_name")) || Arguments->HasField(TEXT("actor_handle"))))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	TSharedPtr<FJsonObject> Result = Actor
		? PhysicsToolUtils::SerializeActorPhysics(Actor, bIncludeComponents, bIncludeConstraints)
		: PhysicsToolUtils::SerializeWorldPhysics(World ? World : FMcpAssetModifier::ResolveWorld(WorldType));

	if (!Result.IsValid() || !Result->GetBoolField(TEXT("valid")))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available for physics summary"));
	}

	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("scope"), Actor ? TEXT("actor") : TEXT("world"));
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Physics summary ready"));
}
