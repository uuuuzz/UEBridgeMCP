// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/QueryRuntimeActorStateTool.h"

#include "Tools/Gameplay/RuntimeGameplayToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

FString UQueryRuntimeActorStateTool::GetToolDescription() const
{
	return TEXT("Return read-only runtime state for an editor or PIE actor, including transform, velocity, tags, components, collision, and optional GAS state.");
}

TMap<FString, FMcpSchemaProperty> UQueryRuntimeActorStateTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor label or object name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle returned by query-level-summary or another actor query")));
	Schema.Add(TEXT("include"), FMcpSchemaProperty::MakeArray(
		TEXT("Sections to include: metadata, transform, velocity, tags, components, collision, controller, gas"),
		TEXT("string")));
	return Schema;
}

FMcpToolResult UQueryRuntimeActorStateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));

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
		true);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	TSet<FString> RequestedSections;
	RuntimeGameplayToolUtils::ExtractIncludeSections(
		Arguments,
		{ TEXT("metadata"), TEXT("transform"), TEXT("velocity"), TEXT("tags"), TEXT("components"), TEXT("collision") },
		RequestedSections);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("world"), RuntimeGameplayToolUtils::SerializeWorld(World));
	Response->SetObjectField(TEXT("actor"), RuntimeGameplayToolUtils::SerializeActorRuntimeState(Actor, Context.SessionId, RequestedSections, Warnings));

	TArray<TSharedPtr<FJsonValue>> RequestedSectionValues;
	for (const FString& RequestedSection : RequestedSections)
	{
		RequestedSectionValues.Add(MakeShareable(new FJsonValueString(RequestedSection)));
	}
	Response->SetArrayField(TEXT("requested_sections"), RequestedSectionValues);
	Response->SetArrayField(TEXT("warnings"), Warnings);
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
	return FMcpToolResult::StructuredJson(Response);
}
