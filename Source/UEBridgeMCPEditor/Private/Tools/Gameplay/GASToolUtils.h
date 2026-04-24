// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;
class UAttributeSet;
class UBlueprint;
class UGameplayAbility;
class UGameplayEffect;
class USCS_Node;

namespace GASToolUtils
{
	UBlueprint* CreateGASBlueprintAsset(const FString& AssetPath, UClass* ParentClass, FString& OutError);
	UBlueprint* CreateGameplayAbilityBlueprintAsset(const FString& AssetPath, UClass* ParentClass, FString& OutError);
	bool CompileAndSaveBlueprint(UBlueprint* Blueprint, bool bCompile, bool bSave, TSharedPtr<FJsonObject>& OutCompile, FString& OutError);

	UBlueprint* LoadBlueprint(const FString& AssetPath, FString& OutError);
	UObject* LoadAssetObject(const FString& AssetPath, FString& OutError);
	UClass* ResolveClass(const FString& ClassOrAssetPath, FString& OutError);
	UClass* ResolveSubclass(const FString& ClassOrAssetPath, UClass* RequiredBaseClass, FString& OutError);
	UGameplayEffect* GetGameplayEffectCDO(UBlueprint* Blueprint);
	UGameplayAbility* GetGameplayAbilityCDO(UBlueprint* Blueprint);

	bool ParseDurationPolicy(const FString& Value, EGameplayEffectDurationType& OutPolicy);
	FString DurationPolicyToString(EGameplayEffectDurationType Policy);
	bool ParseModifierOp(const FString& Value, TEnumAsByte<EGameplayModOp::Type>& OutOp);
	FString ModifierOpToString(TEnumAsByte<EGameplayModOp::Type> Op);
	bool ParseAbilityInstancingPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityInstancingPolicy::Type>& OutPolicy);
	bool ParseAbilityNetExecutionPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type>& OutPolicy);
	bool ParseAbilityReplicationPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityReplicationPolicy::Type>& OutPolicy);

	bool BuildTagContainer(const TArray<TSharedPtr<FJsonValue>>* TagsArray, FGameplayTagContainer& OutTags, FString& OutError);
	TArray<TSharedPtr<FJsonValue>> SerializeTagContainer(const FGameplayTagContainer& Tags);
	bool SetTagContainerProperty(UObject* Target, const FString& PropertyName, const FGameplayTagContainer& Tags, FString& OutError);
	bool SetGameplayEffectGrantedTags(UGameplayEffect* Effect, const FGameplayTagContainer& Tags, FString& OutError);

	bool ResolveGameplayAttribute(const TSharedPtr<FJsonObject>& Source, FGameplayAttribute& OutAttribute, FString& OutError);
	FString AttributeToString(const FGameplayAttribute& Attribute);

	TSharedPtr<FJsonObject> SerializeGameplayEffect(UGameplayEffect* Effect, const FString& AssetPath);
	TSharedPtr<FJsonObject> SerializeGameplayAbility(UGameplayAbility* Ability, const FString& AssetPath);
	TSharedPtr<FJsonObject> SerializeAttributeSetClass(UClass* AttributeSetClass, const FString& AssetPath);
	TSharedPtr<FJsonObject> SerializeActorGASBindings(UBlueprint* Blueprint, const FString& AssetPath);

	bool AddAttributeVariable(UBlueprint* Blueprint, const FString& Name, const FString& Category, FString& OutError);
	bool AddAbilitySystemComponent(UBlueprint* Blueprint, const FString& ComponentName, const FString& ReplicationMode, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool AddClassBindingVariable(UBlueprint* Blueprint, const FString& VariableName, UClass* BaseClass, UClass* BoundClass, const FString& BindingKind, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	USCS_Node* FindSCSNode(UBlueprint* Blueprint, const FString& ComponentName);
}
