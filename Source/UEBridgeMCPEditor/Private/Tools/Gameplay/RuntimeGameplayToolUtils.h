// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class AActor;
class UAbilitySystemComponent;
class UWorld;
struct FHitResult;
struct FMcpToolContext;

namespace RuntimeGameplayToolUtils
{
	FString NormalizeSectionName(const FString& SectionName);
	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues);
	void ExtractIncludeSections(const TSharedPtr<FJsonObject>& Arguments, const TArray<FString>& Defaults, TSet<FString>& OutSections);
	bool HasRequestedSection(const TSet<FString>& RequestedSections, const FString& SectionName);
	bool MatchesAnyFilter(const FString& Value, const TArray<FString>& Filters);

	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Vector);
	TSharedPtr<FJsonObject> VectorToJson(const FVector& Vector);
	bool TryReadVectorField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, FVector& OutVector, FString& OutError);

	TSharedPtr<FJsonObject> SerializeWorld(UWorld* World);
	TSharedPtr<FJsonObject> SerializeActorRuntimeState(
		AActor* Actor,
		const FString& SessionId,
		const TSet<FString>& RequestedSections,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings);
	TSharedPtr<FJsonObject> SerializeHitResult(const FHitResult& Hit, const FString& SessionId);

#if HAS_GAMEPLAY_ABILITIES
	UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* Actor);
	void BuildGameplayTagsPayload(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TArray<FString>& GameplayTagFilters,
		TArray<TSharedPtr<FJsonValue>>& OutTags,
		TArray<TSharedPtr<FJsonValue>>& OutTagDetails);
	void BuildAttributeSetsPayload(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TArray<FString>& AttributeSetFilters,
		TArray<TSharedPtr<FJsonValue>>& OutAttributeSets);
	void BuildAbilitiesPayload(UAbilitySystemComponent* AbilitySystemComponent, TArray<TSharedPtr<FJsonValue>>& OutAbilities);
	TSharedPtr<FJsonObject> SerializeAbilitySystemState(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TSet<FString>& RequestedSections,
		const TArray<FString>& GameplayTagFilters,
		const TArray<FString>& AttributeSetFilters,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings);
#endif
}
