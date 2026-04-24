// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/QueryAbilitySystemStateTool.h"

#include "Tools/Gameplay/RuntimeGameplayToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#ifndef HAS_GAMEPLAY_ABILITIES
#define HAS_GAMEPLAY_ABILITIES 0
#endif

#if HAS_GAMEPLAY_ABILITIES
#include "AbilitySystemComponent.h"
#endif

FString UQueryAbilitySystemStateTool::GetToolDescription() const
{
	return TEXT("Return live AbilitySystemComponent state for an editor or PIE actor: owned gameplay tags, spawned AttributeSets, and activatable abilities.");
}

TMap<FString, FMcpSchemaProperty> UQueryAbilitySystemStateTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor label or object name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle returned by query-level-summary or another actor query")));
	Schema.Add(TEXT("include"), FMcpSchemaProperty::MakeArray(
		TEXT("Sections to include: gameplay_tags, attributes, abilities"),
		TEXT("string")));
	Schema.Add(TEXT("attribute_set_filters"), FMcpSchemaProperty::MakeArray(
		TEXT("Optional wildcard or substring filters for AttributeSet class names or paths"),
		TEXT("string")));
	Schema.Add(TEXT("gameplay_tag_filters"), FMcpSchemaProperty::MakeArray(
		TEXT("Optional wildcard or substring filters applied to returned gameplay tags"),
		TEXT("string")));
	return Schema;
}

FMcpToolResult UQueryAbilitySystemStateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
#if !HAS_GAMEPLAY_ABILITIES
	return FMcpToolResult::StructuredError(
		TEXT("UEBMCP_GAMEPLAY_ABILITIES_UNAVAILABLE"),
		TEXT("GameplayAbilities support is not available in this build"));
#else
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

	UAbilitySystemComponent* AbilitySystemComponent = RuntimeGameplayToolUtils::ResolveAbilitySystemComponent(Actor);
	if (!AbilitySystemComponent)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Details->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_ABILITY_SYSTEM_NOT_FOUND"),
			TEXT("Actor does not expose an AbilitySystemComponent"),
			Details);
	}

	TSet<FString> RequestedSections;
	RuntimeGameplayToolUtils::ExtractIncludeSections(
		Arguments,
		{ TEXT("gameplay_tags"), TEXT("attributes"), TEXT("abilities") },
		RequestedSections);

	TArray<FString> AttributeSetFilters;
	TArray<FString> GameplayTagFilters;
	RuntimeGameplayToolUtils::ExtractStringArrayField(Arguments, TEXT("attribute_set_filters"), AttributeSetFilters);
	RuntimeGameplayToolUtils::ExtractStringArrayField(Arguments, TEXT("gameplay_tag_filters"), GameplayTagFilters);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("world"), RuntimeGameplayToolUtils::SerializeWorld(World));
	TSet<FString> ActorSections;
	ActorSections.Add(TEXT("metadata"));
	ActorSections.Add(TEXT("transform"));
	Response->SetObjectField(TEXT("actor"), RuntimeGameplayToolUtils::SerializeActorRuntimeState(
		Actor,
		Context.SessionId,
		ActorSections,
		Warnings));
	Response->SetObjectField(TEXT("ability_system"), RuntimeGameplayToolUtils::SerializeAbilitySystemState(
		AbilitySystemComponent,
		RequestedSections,
		GameplayTagFilters,
		AttributeSetFilters,
		Warnings));

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
#endif
}
