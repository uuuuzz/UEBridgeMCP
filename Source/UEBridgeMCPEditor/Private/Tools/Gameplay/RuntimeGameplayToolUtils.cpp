// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/RuntimeGameplayToolUtils.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpV2ToolUtils.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

#ifndef HAS_GAMEPLAY_ABILITIES
#define HAS_GAMEPLAY_ABILITIES 0
#endif

#if HAS_GAMEPLAY_ABILITIES
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#endif

namespace
{
	void AppendWarning(TArray<TSharedPtr<FJsonValue>>& OutWarnings, const FString& Warning)
	{
		OutWarnings.Add(MakeShareable(new FJsonValueString(Warning)));
	}

	TSharedPtr<FJsonObject> SerializeComponentRuntime(UActorComponent* Component, bool bIncludeCollision)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Component)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("name"), Component->GetName());
		Object->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		Object->SetBoolField(TEXT("active"), Component->IsActive());
		Object->SetBoolField(TEXT("registered"), Component->IsRegistered());

		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			Object->SetObjectField(TEXT("world_location"), RuntimeGameplayToolUtils::VectorToJson(SceneComponent->GetComponentLocation()));
			Object->SetObjectField(TEXT("relative_transform"), McpV2ToolUtils::SerializeTransform(SceneComponent->GetRelativeTransform()));
			Object->SetStringField(TEXT("mobility"), SceneComponent->Mobility == EComponentMobility::Movable
				? TEXT("Movable")
				: (SceneComponent->Mobility == EComponentMobility::Stationary ? TEXT("Stationary") : TEXT("Static")));
		}

		if (bIncludeCollision)
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				Object->SetObjectField(TEXT("collision"), PhysicsToolUtils::SerializePrimitiveComponent(Primitive));
			}
		}

		return Object;
	}
}

namespace RuntimeGameplayToolUtils
{
	FString NormalizeSectionName(const FString& SectionName)
	{
		FString Normalized = SectionName;
		Normalized.TrimStartAndEndInline();
		Normalized.ToLowerInline();
		return Normalized;
	}

	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, ArrayField) || !ArrayField)
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

	void ExtractIncludeSections(const TSharedPtr<FJsonObject>& Arguments, const TArray<FString>& Defaults, TSet<FString>& OutSections)
	{
		OutSections.Reset();

		const TArray<TSharedPtr<FJsonValue>>* IncludeArray = nullptr;
		if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("include"), IncludeArray) && IncludeArray && IncludeArray->Num() > 0)
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

		for (const FString& DefaultSection : Defaults)
		{
			OutSections.Add(NormalizeSectionName(DefaultSection));
		}
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

	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	TSharedPtr<FJsonObject> VectorToJson(const FVector& Vector)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		return Object;
	}

	bool TryReadVectorField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, FVector& OutVector, FString& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(FieldName))
		{
			OutError = FString::Printf(TEXT("'%s' is required"), *FieldName);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		if (Arguments->TryGetArrayField(FieldName, ArrayField) && ArrayField)
		{
			if (ArrayField->Num() < 3)
			{
				OutError = FString::Printf(TEXT("'%s' must contain at least three numbers"), *FieldName);
				return false;
			}
			OutVector = FVector((*ArrayField)[0]->AsNumber(), (*ArrayField)[1]->AsNumber(), (*ArrayField)[2]->AsNumber());
			return true;
		}

		const TSharedPtr<FJsonObject>* ObjectField = nullptr;
		if (Arguments->TryGetObjectField(FieldName, ObjectField) && ObjectField && (*ObjectField).IsValid())
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (!(*ObjectField)->TryGetNumberField(TEXT("x"), X)
				|| !(*ObjectField)->TryGetNumberField(TEXT("y"), Y)
				|| !(*ObjectField)->TryGetNumberField(TEXT("z"), Z))
			{
				OutError = FString::Printf(TEXT("'%s' object must include numeric x, y, and z fields"), *FieldName);
				return false;
			}
			OutVector = FVector(X, Y, Z);
			return true;
		}

		OutError = FString::Printf(TEXT("'%s' must be either [x,y,z] or {x,y,z}"), *FieldName);
		return false;
	}

	TSharedPtr<FJsonObject> SerializeWorld(UWorld* World)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetBoolField(TEXT("valid"), World != nullptr);
		if (!World)
		{
			return Object;
		}

		Object->SetStringField(TEXT("name"), World->GetName());
		Object->SetStringField(TEXT("path"), World->GetPathName());
		Object->SetStringField(TEXT("type"), LexToString(World->WorldType));
		Object->SetStringField(TEXT("map_name"), World->GetMapName());
		Object->SetBoolField(TEXT("is_pie"), World->WorldType == EWorldType::PIE);
		Object->SetNumberField(TEXT("time_seconds"), World->GetTimeSeconds());
		Object->SetNumberField(TEXT("delta_seconds"), World->GetDeltaSeconds());
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeActorRuntimeState(
		AActor* Actor,
		const FString& SessionId,
		const TSet<FString>& RequestedSections,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetBoolField(TEXT("valid"), Actor != nullptr);
		if (!Actor)
		{
			return Object;
		}

		FMcpEditorSessionManager::Get().RememberActor(
			SessionId,
			Actor->GetWorld() ? Actor->GetWorld()->GetPathName() : FString(),
			Actor);

		if (HasRequestedSection(RequestedSections, TEXT("metadata")))
		{
			Object->SetStringField(TEXT("name"), Actor->GetName());
			Object->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
			Object->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
			Object->SetStringField(TEXT("object_path"), Actor->GetPathName());
			Object->SetObjectField(TEXT("handle"), McpV2ToolUtils::MakeEntityHandle(
				TEXT("actor"),
				SessionId,
				Actor->GetWorld() ? Actor->GetWorld()->GetPathName() : FString(),
				Actor->GetPathName(),
				Actor->GetActorNameOrLabel()));
		}

		if (HasRequestedSection(RequestedSections, TEXT("transform")))
		{
			Object->SetObjectField(TEXT("transform"), McpV2ToolUtils::SerializeTransform(Actor->GetActorTransform()));
			if (TSharedPtr<FJsonObject> BoundsObject = McpV2ToolUtils::SerializeActorBounds(Actor))
			{
				Object->SetObjectField(TEXT("bounds"), BoundsObject);
			}
		}

		if (HasRequestedSection(RequestedSections, TEXT("velocity")))
		{
			TSharedPtr<FJsonObject> VelocityObject = MakeShareable(new FJsonObject);
			VelocityObject->SetObjectField(TEXT("linear"), VectorToJson(Actor->GetVelocity()));
			if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
			{
				VelocityObject->SetObjectField(TEXT("physics_linear"), VectorToJson(RootPrimitive->GetPhysicsLinearVelocity()));
				VelocityObject->SetObjectField(TEXT("physics_angular_degrees"), VectorToJson(RootPrimitive->GetPhysicsAngularVelocityInDegrees()));
				VelocityObject->SetBoolField(TEXT("root_simulating_physics"), RootPrimitive->IsSimulatingPhysics());
			}
			Object->SetObjectField(TEXT("velocity"), VelocityObject);
		}

		if (HasRequestedSection(RequestedSections, TEXT("tags")))
		{
			TArray<TSharedPtr<FJsonValue>> Tags;
			for (const FName& Tag : Actor->Tags)
			{
				Tags.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
			}
			Object->SetArrayField(TEXT("tags"), Tags);
		}

		const bool bIncludeComponents = HasRequestedSection(RequestedSections, TEXT("components"));
		const bool bIncludeCollision = HasRequestedSection(RequestedSections, TEXT("collision"));
		if (bIncludeComponents || bIncludeCollision)
		{
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			TArray<TSharedPtr<FJsonValue>> ComponentArray;
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}
				if (!bIncludeComponents && !Cast<UPrimitiveComponent>(Component))
				{
					continue;
				}
				ComponentArray.Add(MakeShareable(new FJsonValueObject(SerializeComponentRuntime(Component, bIncludeCollision))));
			}
			Object->SetArrayField(TEXT("components"), ComponentArray);
			Object->SetNumberField(TEXT("component_count"), ComponentArray.Num());
		}

		if (HasRequestedSection(RequestedSections, TEXT("controller")))
		{
			if (const APawn* Pawn = Cast<APawn>(Actor))
			{
				TSharedPtr<FJsonObject> ControllerObject = MakeShareable(new FJsonObject);
				ControllerObject->SetStringField(TEXT("controller"), Pawn->GetController() ? Pawn->GetController()->GetPathName() : FString());
				ControllerObject->SetStringField(TEXT("player_state"), Pawn->GetPlayerState() ? Pawn->GetPlayerState()->GetPathName() : FString());
				Object->SetObjectField(TEXT("controller"), ControllerObject);
			}
			else
			{
				AppendWarning(OutWarnings, TEXT("controller section requested for a non-Pawn actor"));
				Object->SetField(TEXT("controller"), MakeShareable(new FJsonValueNull()));
			}
		}

#if HAS_GAMEPLAY_ABILITIES
		if (HasRequestedSection(RequestedSections, TEXT("gas")))
		{
			UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent(Actor);
			if (!AbilitySystemComponent)
			{
				Object->SetBoolField(TEXT("gas_available"), false);
				AppendWarning(OutWarnings, TEXT("Actor does not expose an AbilitySystemComponent"));
			}
			else
			{
				Object->SetBoolField(TEXT("gas_available"), true);
				TSet<FString> GasSections;
				GasSections.Add(TEXT("gameplay_tags"));
				GasSections.Add(TEXT("attributes"));
				GasSections.Add(TEXT("abilities"));
				Object->SetObjectField(TEXT("gas"), SerializeAbilitySystemState(AbilitySystemComponent, GasSections, {}, {}, OutWarnings));
			}
		}
#endif

		return Object;
	}

	TSharedPtr<FJsonObject> SerializeHitResult(const FHitResult& Hit, const FString& SessionId)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetBoolField(TEXT("blocking_hit"), Hit.bBlockingHit);
		Object->SetBoolField(TEXT("start_penetrating"), Hit.bStartPenetrating);
		Object->SetNumberField(TEXT("time"), Hit.Time);
		Object->SetNumberField(TEXT("distance"), Hit.Distance);
		Object->SetObjectField(TEXT("location"), VectorToJson(Hit.Location));
		Object->SetObjectField(TEXT("impact_point"), VectorToJson(Hit.ImpactPoint));
		Object->SetObjectField(TEXT("normal"), VectorToJson(Hit.Normal));
		Object->SetObjectField(TEXT("impact_normal"), VectorToJson(Hit.ImpactNormal));
		Object->SetStringField(TEXT("bone_name"), Hit.BoneName.ToString());
		Object->SetStringField(TEXT("component_name"), Hit.GetComponent() ? Hit.GetComponent()->GetName() : FString());
		Object->SetStringField(TEXT("component_class"), Hit.GetComponent() ? Hit.GetComponent()->GetClass()->GetName() : FString());

		if (AActor* HitActor = Hit.GetActor())
		{
			FMcpEditorSessionManager::Get().RememberActor(
				SessionId,
				HitActor->GetWorld() ? HitActor->GetWorld()->GetPathName() : FString(),
				HitActor);
			Object->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(HitActor, SessionId, false, false));
		}
		else
		{
			Object->SetField(TEXT("actor"), MakeShareable(new FJsonValueNull()));
		}

		return Object;
	}

#if HAS_GAMEPLAY_ABILITIES
	UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		if (IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Actor))
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
		if (!AbilitySystemComponent)
		{
			return;
		}

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
		if (!AbilitySystemComponent)
		{
			return;
		}

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
		if (!AbilitySystemComponent)
		{
			return;
		}

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

			if (UGameplayAbility* Ability = AbilitySpec.Ability)
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

	TSharedPtr<FJsonObject> SerializeAbilitySystemState(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TSet<FString>& RequestedSections,
		const TArray<FString>& GameplayTagFilters,
		const TArray<FString>& AttributeSetFilters,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetBoolField(TEXT("valid"), AbilitySystemComponent != nullptr);
		if (!AbilitySystemComponent)
		{
			return Object;
		}

		Object->SetStringField(TEXT("component_name"), AbilitySystemComponent->GetName());
		Object->SetStringField(TEXT("component_class"), AbilitySystemComponent->GetClass()->GetPathName());
		Object->SetStringField(TEXT("owner_name"), AbilitySystemComponent->GetOwner() ? AbilitySystemComponent->GetOwner()->GetActorNameOrLabel() : FString());
		Object->SetBoolField(TEXT("is_registered"), AbilitySystemComponent->IsRegistered());
		Object->SetNumberField(TEXT("activatable_ability_count"), AbilitySystemComponent->GetActivatableAbilities().Num());
		Object->SetNumberField(TEXT("spawned_attribute_set_count"), AbilitySystemComponent->GetSpawnedAttributes().Num());

		if (HasRequestedSection(RequestedSections, TEXT("gameplay_tags")))
		{
			TArray<TSharedPtr<FJsonValue>> GameplayTags;
			TArray<TSharedPtr<FJsonValue>> GameplayTagDetails;
			BuildGameplayTagsPayload(AbilitySystemComponent, GameplayTagFilters, GameplayTags, GameplayTagDetails);
			Object->SetArrayField(TEXT("gameplay_tags"), GameplayTags);
			Object->SetArrayField(TEXT("gameplay_tag_details"), GameplayTagDetails);
			if (GameplayTagFilters.Num() > 0 && GameplayTags.Num() == 0)
			{
				AppendWarning(OutWarnings, TEXT("No gameplay tag matched the provided gameplay_tag_filters"));
			}
		}

		if (HasRequestedSection(RequestedSections, TEXT("attributes")))
		{
			TArray<TSharedPtr<FJsonValue>> AttributeSets;
			BuildAttributeSetsPayload(AbilitySystemComponent, AttributeSetFilters, AttributeSets);
			Object->SetArrayField(TEXT("attribute_sets"), AttributeSets);
			if (AttributeSetFilters.Num() > 0 && AttributeSets.Num() == 0)
			{
				AppendWarning(OutWarnings, TEXT("No AttributeSet matched the provided attribute_set_filters"));
			}
		}

		if (HasRequestedSection(RequestedSections, TEXT("abilities")))
		{
			TArray<TSharedPtr<FJsonValue>> Abilities;
			BuildAbilitiesPayload(AbilitySystemComponent, Abilities);
			Object->SetArrayField(TEXT("abilities"), Abilities);
		}

		return Object;
	}
#endif
}
