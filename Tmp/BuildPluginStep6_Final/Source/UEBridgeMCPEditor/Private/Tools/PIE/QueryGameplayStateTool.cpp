// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/QueryGameplayStateTool.h"
#include "Utils/McpAssetModifier.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Serialization/JsonSerializer.h"

#ifndef HAS_GAMEPLAY_ABILITIES
#define HAS_GAMEPLAY_ABILITIES 0
#endif

#ifndef HAS_GAMEPLAY_STATE_TREE
#define HAS_GAMEPLAY_STATE_TREE 0
#endif

#if HAS_GAMEPLAY_ABILITIES
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#endif

#if HAS_GAMEPLAY_STATE_TREE
#include "Components/StateTreeComponent.h"
#include "StateTreeExecutionTypes.h"
#endif

namespace QueryGameplayStateToolPrivate
{
	FString NormalizeSectionName(const FString& SectionName)
	{
		FString Normalized = SectionName;
		Normalized.TrimStartAndEndInline();
		Normalized.ToLowerInline();
		return Normalized;
	}

	void AppendWarning(TArray<TSharedPtr<FJsonValue>>& OutWarnings, const FString& WarningMessage)
	{
		OutWarnings.Add(MakeShareable(new FJsonValueString(WarningMessage)));
	}

	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		if (!Arguments->TryGetArrayField(FieldName, ArrayField) || !ArrayField)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *ArrayField)
		{
			if (Value.IsValid())
			{
				OutValues.Add(Value->AsString());
			}
		}
	}

	void ExtractIncludeSections(const TSharedPtr<FJsonObject>& Arguments, TSet<FString>& OutSections)
	{
		const TArray<TSharedPtr<FJsonValue>>* IncludeArray = nullptr;
		if (Arguments->TryGetArrayField(TEXT("include"), IncludeArray) && IncludeArray && IncludeArray->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& Value : *IncludeArray)
			{
				if (Value.IsValid())
				{
					OutSections.Add(NormalizeSectionName(Value->AsString()));
				}
			}
			return;
		}

		OutSections.Add(TEXT("gameplay_tags"));
		OutSections.Add(TEXT("attributes"));
		OutSections.Add(TEXT("abilities"));
		OutSections.Add(TEXT("montage"));
		OutSections.Add(TEXT("state_tree"));
	}

	bool HasRequestedSection(const TSet<FString>& RequestedSections, const FString& SectionName)
	{
		return RequestedSections.Contains(NormalizeSectionName(SectionName));
	}

	bool MatchesAnyFilter(const FString& Value, const TArray<FString>& Filters)
	{
		if (Filters.Num() == 0)
		{
			return true;
		}

		for (const FString& Filter : Filters)
		{
			if (Filter.IsEmpty())
			{
				continue;
			}

			if (Filter.Contains(TEXT("*")) || Filter.Contains(TEXT("?")))
			{
				if (Value.MatchesWildcard(Filter, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			else if (Value.Equals(Filter, ESearchCase::IgnoreCase) || Value.Contains(Filter, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	void AppendStringValues(const TArray<FString>& Values, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		for (const FString& Value : Values)
		{
			OutArray.Add(MakeShareable(new FJsonValueString(Value)));
		}
	}

	USkeletalMeshComponent* FindSkeletalMeshComponent(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}

#if HAS_GAMEPLAY_ABILITIES
	UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Actor);
		if (AbilitySystemInterface)
		{
			if (UAbilitySystemComponent* AbilitySystemComponent = AbilitySystemInterface->GetAbilitySystemComponent())
			{
				return AbilitySystemComponent;
			}
		}

		return Actor->FindComponentByClass<UAbilitySystemComponent>();
	}

	void BuildGameplayTagsPayload(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TArray<FString>& GameplayTagFilters,
		TArray<TSharedPtr<FJsonValue>>& OutTags,
		TArray<TSharedPtr<FJsonValue>>& OutTagDetails)
	{
		FGameplayTagContainer OwnedTags;
		AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);
		for (const FGameplayTag& GameplayTag : OwnedTags)
		{
			const FString TagString = GameplayTag.ToString();
			if (!MatchesAnyFilter(TagString, GameplayTagFilters))
			{
				continue;
			}

			OutTags.Add(MakeShareable(new FJsonValueString(TagString)));

			TSharedPtr<FJsonObject> TagDetail = MakeShareable(new FJsonObject);
			TagDetail->SetStringField(TEXT("tag"), TagString);
			TagDetail->SetNumberField(TEXT("count"), AbilitySystemComponent->GetTagCount(GameplayTag));
			OutTagDetails.Add(MakeShareable(new FJsonValueObject(TagDetail)));
		}
	}

	void BuildAttributeSetsPayload(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TArray<FString>& AttributeSetFilters,
		TArray<TSharedPtr<FJsonValue>>& OutAttributeSets)
	{
		for (const UAttributeSet* AttributeSet : AbilitySystemComponent->GetSpawnedAttributes())
		{
			if (!AttributeSet)
			{
				continue;
			}

			const FString SetClassName = AttributeSet->GetClass()->GetName();
			const FString SetClassPath = AttributeSet->GetClass()->GetPathName();
			if (!MatchesAnyFilter(SetClassName, AttributeSetFilters)
				&& !MatchesAnyFilter(SetClassPath, AttributeSetFilters))
			{
				continue;
			}

			TArray<TSharedPtr<FJsonValue>> AttributeValues;
			for (TFieldIterator<FProperty> PropertyIterator(AttributeSet->GetClass()); PropertyIterator; ++PropertyIterator)
			{
				FProperty* Property = *PropertyIterator;
				if (!Property)
				{
					continue;
				}

				const bool bIsGameplayAttributeData = FGameplayAttribute::IsGameplayAttributeDataProperty(Property);
				const bool bIsNumericProperty = CastField<FNumericProperty>(Property) != nullptr;
				if (!bIsGameplayAttributeData && !bIsNumericProperty)
				{
					continue;
				}

				const FGameplayAttribute GameplayAttribute(Property);
				if (!GameplayAttribute.IsValid())
				{
					continue;
				}

				TSharedPtr<FJsonObject> AttributeObject = MakeShareable(new FJsonObject);
				AttributeObject->SetStringField(TEXT("name"), Property->GetName());
				AttributeObject->SetStringField(TEXT("property_type"), Property->GetCPPType());
				AttributeObject->SetNumberField(TEXT("current_value"), AbilitySystemComponent->GetNumericAttribute(GameplayAttribute));
				AttributeObject->SetNumberField(TEXT("base_value"), AbilitySystemComponent->GetNumericAttributeBase(GameplayAttribute));
				AttributeValues.Add(MakeShareable(new FJsonValueObject(AttributeObject)));
			}

			TSharedPtr<FJsonObject> AttributeSetObject = MakeShareable(new FJsonObject);
			AttributeSetObject->SetStringField(TEXT("class"), SetClassName);
			AttributeSetObject->SetStringField(TEXT("class_path"), SetClassPath);
			AttributeSetObject->SetArrayField(TEXT("attributes"), AttributeValues);
			OutAttributeSets.Add(MakeShareable(new FJsonValueObject(AttributeSetObject)));
		}
	}

	void BuildAbilitiesPayload(UAbilitySystemComponent* AbilitySystemComponent, TArray<TSharedPtr<FJsonValue>>& OutAbilities)
	{
		for (const FGameplayAbilitySpec& AbilitySpec : AbilitySystemComponent->GetActivatableAbilities())
		{
			TSharedPtr<FJsonObject> AbilityObject = MakeShareable(new FJsonObject);
			AbilityObject->SetNumberField(TEXT("level"), AbilitySpec.Level);
			AbilityObject->SetNumberField(TEXT("input_id"), AbilitySpec.InputID);
			AbilityObject->SetBoolField(TEXT("is_active"), AbilitySpec.IsActive());
			AbilityObject->SetNumberField(TEXT("active_count"), AbilitySpec.ActiveCount);
			AbilityObject->SetStringField(TEXT("handle"), AbilitySpec.Handle.ToString());
			if (AbilitySpec.SourceObject.IsValid())
			{
				AbilityObject->SetStringField(TEXT("source_object"), AbilitySpec.SourceObject->GetPathName());
			}

			UGameplayAbility* Ability = AbilitySpec.Ability;
			if (Ability)
			{
				AbilityObject->SetStringField(TEXT("name"), Ability->GetName());
				AbilityObject->SetStringField(TEXT("class"), Ability->GetClass()->GetName());
				AbilityObject->SetStringField(TEXT("class_path"), Ability->GetClass()->GetPathName());

				TArray<TSharedPtr<FJsonValue>> AbilityTags;
				for (const FGameplayTag& AbilityTag : Ability->GetAssetTags())
				{
					AbilityTags.Add(MakeShareable(new FJsonValueString(AbilityTag.ToString())));
				}
				AbilityObject->SetArrayField(TEXT("ability_tags"), AbilityTags);

				AbilityObject->SetArrayField(TEXT("activation_owned_tags"), TArray<TSharedPtr<FJsonValue>>());
			}
			else
			{
				AbilityObject->SetStringField(TEXT("name"), TEXT("None"));
				AbilityObject->SetStringField(TEXT("class"), TEXT("None"));
			}

			TArray<TSharedPtr<FJsonValue>> DynamicSourceTags;
			for (const FGameplayTag& DynamicTag : AbilitySpec.GetDynamicSpecSourceTags())
			{
				DynamicSourceTags.Add(MakeShareable(new FJsonValueString(DynamicTag.ToString())));
			}
			AbilityObject->SetArrayField(TEXT("dynamic_source_tags"), DynamicSourceTags);
			OutAbilities.Add(MakeShareable(new FJsonValueObject(AbilityObject)));
		}
	}
#endif

#if HAS_GAMEPLAY_STATE_TREE
	FString LexToStringStateTreeRunStatus(const EStateTreeRunStatus RunStatus)
	{
		switch (RunStatus)
		{
		case EStateTreeRunStatus::Running:
			return TEXT("Running");
		case EStateTreeRunStatus::Stopped:
			return TEXT("Stopped");
		case EStateTreeRunStatus::Succeeded:
			return TEXT("Succeeded");
		case EStateTreeRunStatus::Failed:
			return TEXT("Failed");
		case EStateTreeRunStatus::Unset:
		default:
			return TEXT("Unset");
		}
	}
#endif
}

FString UQueryGameplayStateTool::GetToolDescription() const
{
	return TEXT("Query runtime gameplay state for a PIE actor, including montage, gameplay tags, GAS attributes, abilities, and optional StateTree state. "
		"Supports section filters and returns a structured envelope with warnings and diagnostics.");
}

TMap<FString, FMcpSchemaProperty> UQueryGameplayStateTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("World selection: 'pie' or 'auto'"), {TEXT("pie"), TEXT("auto")}));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label to query"), true));
	Schema.Add(TEXT("include"), FMcpSchemaProperty::MakeArray(
		TEXT("Sections to include: gameplay_tags, attributes, abilities, montage, state_tree"), TEXT("string")));
	Schema.Add(TEXT("attribute_set_filters"), FMcpSchemaProperty::MakeArray(
		TEXT("Optional wildcard or substring filters for AttributeSet class names or paths"), TEXT("string")));
	Schema.Add(TEXT("gameplay_tag_filters"), FMcpSchemaProperty::MakeArray(
		TEXT("Optional wildcard or substring filters applied to returned gameplay tags"), TEXT("string")));

	return Schema;
}

TArray<FString> UQueryGameplayStateTool::GetRequiredParams() const
{
	return {TEXT("actor_name")};
}

FMcpToolResult UQueryGameplayStateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString ActorName;
	if (!GetStringArg(Arguments, TEXT("actor_name"), ActorName))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actor_name' required"));
	}

	const FString RequestedWorld = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("pie"));
	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorld == TEXT("auto") ? TEXT("pie") : RequestedWorld);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_PIE_NOT_RUNNING"), TEXT("PIE is not running"));
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_ACTOR_NOT_FOUND"),
			FString::Printf(TEXT("Actor '%s' was not found in the PIE world"), *ActorName));
	}

	TSet<FString> RequestedSections;
	QueryGameplayStateToolPrivate::ExtractIncludeSections(Arguments, RequestedSections);

	TArray<FString> AttributeSetFilters;
	TArray<FString> GameplayTagFilters;
	QueryGameplayStateToolPrivate::ExtractStringArrayField(Arguments, TEXT("attribute_set_filters"), AttributeSetFilters);
	QueryGameplayStateToolPrivate::ExtractStringArrayField(Arguments, TEXT("gameplay_tag_filters"), GameplayTagFilters);

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;

	TSharedPtr<FJsonObject> WorldObject = MakeShareable(new FJsonObject);

	WorldObject->SetStringField(TEXT("type"), TEXT("pie"));
	WorldObject->SetStringField(TEXT("map_name"), World->GetMapName());
	WorldObject->SetBoolField(TEXT("is_pie_running"), true);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("query-gameplay-state"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("world"), WorldObject);
	Response->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
	Response->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Response->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());

	if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("montage")))
	{
		USkeletalMeshComponent* SkeletalMeshComponent = QueryGameplayStateToolPrivate::FindSkeletalMeshComponent(Actor);
		if (SkeletalMeshComponent && SkeletalMeshComponent->GetAnimInstance())
		{
			UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
			UAnimMontage* CurrentMontage = AnimInstance->GetCurrentActiveMontage();
			if (CurrentMontage)
			{
				TSharedPtr<FJsonObject> MontageObject = MakeShareable(new FJsonObject);
				MontageObject->SetStringField(TEXT("name"), CurrentMontage->GetName());
				MontageObject->SetStringField(TEXT("path"), CurrentMontage->GetPathName());
				MontageObject->SetNumberField(TEXT("position"), AnimInstance->Montage_GetPosition(CurrentMontage));
				MontageObject->SetBoolField(TEXT("is_playing"), AnimInstance->Montage_IsPlaying(CurrentMontage));
				Response->SetObjectField(TEXT("montage"), MontageObject);
			}
			else
			{
				Response->SetField(TEXT("montage"), MakeShareable(new FJsonValueNull()));
			}
		}
		else
		{
			Response->SetField(TEXT("montage"), MakeShareable(new FJsonValueNull()));
			QueryGameplayStateToolPrivate::AppendWarning(WarningsArray, TEXT("No skeletal mesh component with an animation instance was found for montage inspection"));
		}
	}

#if HAS_GAMEPLAY_ABILITIES
	const bool bNeedsGasSections = QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("gameplay_tags"))
		|| QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("attributes"))
		|| QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("abilities"));
	if (bNeedsGasSections)
	{
		UAbilitySystemComponent* AbilitySystemComponent = QueryGameplayStateToolPrivate::ResolveAbilitySystemComponent(Actor);
		if (!AbilitySystemComponent)
		{
			Response->SetBoolField(TEXT("gas_available"), false);
			QueryGameplayStateToolPrivate::AppendWarning(WarningsArray, TEXT("Actor does not expose an AbilitySystemComponent"));
		}
		else
		{
			Response->SetBoolField(TEXT("gas_available"), true);

			if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("gameplay_tags")))
			{
				TArray<TSharedPtr<FJsonValue>> GameplayTags;
				TArray<TSharedPtr<FJsonValue>> GameplayTagDetails;
				QueryGameplayStateToolPrivate::BuildGameplayTagsPayload(
					AbilitySystemComponent,
					GameplayTagFilters,
					GameplayTags,
					GameplayTagDetails);
				Response->SetArrayField(TEXT("gameplay_tags"), GameplayTags);
				Response->SetArrayField(TEXT("gameplay_tag_details"), GameplayTagDetails);
			}

			if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("attributes")))
			{
				TArray<TSharedPtr<FJsonValue>> AttributeSets;
				QueryGameplayStateToolPrivate::BuildAttributeSetsPayload(
					AbilitySystemComponent,
					AttributeSetFilters,
					AttributeSets);
				Response->SetArrayField(TEXT("attribute_sets"), AttributeSets);
				if (AttributeSetFilters.Num() > 0 && AttributeSets.Num() == 0)
				{
					QueryGameplayStateToolPrivate::AppendWarning(WarningsArray, TEXT("No AttributeSet matched the provided attribute_set_filters"));
				}
			}

			if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("abilities")))
			{
				TArray<TSharedPtr<FJsonValue>> Abilities;
				QueryGameplayStateToolPrivate::BuildAbilitiesPayload(AbilitySystemComponent, Abilities);
				Response->SetArrayField(TEXT("abilities"), Abilities);
			}
		}
	}
#else
	if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("gameplay_tags"))
		|| QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("attributes"))
		|| QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("abilities")))
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_GAMEPLAY_ABILITIES_UNAVAILABLE"),
			TEXT("GameplayAbilities support is not available in this build"));
	}
#endif

#if HAS_GAMEPLAY_STATE_TREE
	if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("state_tree")))
	{
		UStateTreeComponent* StateTreeComponent = Actor->FindComponentByClass<UStateTreeComponent>();
		if (!StateTreeComponent)
		{
			Response->SetField(TEXT("state_tree"), MakeShareable(new FJsonValueNull()));
			QueryGameplayStateToolPrivate::AppendWarning(WarningsArray, TEXT("Actor does not have a StateTreeComponent"));
		}
		else
		{
			TSharedPtr<FJsonObject> StateTreeObject = MakeShareable(new FJsonObject);
			const EStateTreeRunStatus RunStatus = StateTreeComponent->GetStateTreeRunStatus();
			StateTreeObject->SetStringField(TEXT("component_name"), StateTreeComponent->GetName());
			StateTreeObject->SetStringField(TEXT("run_status"), QueryGameplayStateToolPrivate::LexToStringStateTreeRunStatus(RunStatus));
			StateTreeObject->SetBoolField(TEXT("is_running"), StateTreeComponent->IsRunning());
			StateTreeObject->SetBoolField(TEXT("is_paused"), StateTreeComponent->IsPaused());
#if WITH_GAMEPLAY_DEBUGGER
			TArray<TSharedPtr<FJsonValue>> ActiveStateNames;
			TArray<FName> ActiveStates = StateTreeComponent->GetActiveStateNames();
			for (const FName& ActiveState : ActiveStates)
			{
				ActiveStateNames.Add(MakeShareable(new FJsonValueString(ActiveState.ToString())));
			}
			StateTreeObject->SetArrayField(TEXT("active_state_names"), ActiveStateNames);
#endif
			Response->SetObjectField(TEXT("state_tree"), StateTreeObject);
		}
	}
#else
	if (QueryGameplayStateToolPrivate::HasRequestedSection(RequestedSections, TEXT("state_tree")))
	{
		QueryGameplayStateToolPrivate::AppendWarning(WarningsArray, TEXT("StateTree runtime support is not available in this build"));
	}
#endif

	TArray<TSharedPtr<FJsonValue>> RequestedSectionValues;
	for (const FString& RequestedSection : RequestedSections)
	{
		RequestedSectionValues.Add(MakeShareable(new FJsonValueString(RequestedSection)));
	}
	Response->SetArrayField(TEXT("requested_sections"), RequestedSectionValues);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response);
}