// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateGameplayAbilityTool.h"

#include "Tools/Gameplay/GASToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Abilities/GameplayAbility.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "ScopedTransaction.h"

namespace
{
	bool SetEnumByteProperty(UObject* Target, const TCHAR* PropertyName, int64 EnumValue, FString& OutError)
	{
		FProperty* Property = Target ? Target->GetClass()->FindPropertyByName(PropertyName) : nullptr;
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			ByteProperty->SetPropertyValue(ByteProperty->ContainerPtrToValuePtr<void>(Target), static_cast<uint8>(EnumValue));
			return true;
		}
		OutError = FString::Printf(TEXT("Enum byte property not found: %s"), PropertyName);
		return false;
	}

	bool SetClassProperty(UObject* Target, const TCHAR* PropertyName, UClass* ClassValue, FString& OutError)
	{
		FProperty* Property = Target ? Target->GetClass()->FindPropertyByName(PropertyName) : nullptr;
		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			ClassProperty->SetObjectPropertyValue(ClassProperty->ContainerPtrToValuePtr<void>(Target), ClassValue);
			return true;
		}
		OutError = FString::Printf(TEXT("Class property not found: %s"), PropertyName);
		return false;
	}
}

FString UCreateGameplayAbilityTool::GetToolDescription() const
{
	return TEXT("Create a GameplayAbility Blueprint and configure common GAS v1 defaults such as tags, policies, cost, and cooldown classes.");
}

TMap<FString, FMcpSchemaProperty> UCreateGameplayAbilityTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("GameplayAbility Blueprint asset path"), true));
	Schema.Add(TEXT("parent_class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional parent class path; defaults to /Script/GameplayAbilities.GameplayAbility")));
	Schema.Add(TEXT("ability_tags"), FMcpSchemaProperty::MakeArray(TEXT("Existing GameplayTags to assign as ability asset tags"), TEXT("string")));
	Schema.Add(TEXT("activation_owned_tags"), FMcpSchemaProperty::MakeArray(TEXT("Existing GameplayTags granted while active"), TEXT("string")));
	Schema.Add(TEXT("activation_required_tags"), FMcpSchemaProperty::MakeArray(TEXT("Existing GameplayTags required to activate"), TEXT("string")));
	Schema.Add(TEXT("activation_blocked_tags"), FMcpSchemaProperty::MakeArray(TEXT("Existing GameplayTags blocking activation"), TEXT("string")));
	Schema.Add(TEXT("instancing_policy"), FMcpSchemaProperty::MakeEnum(TEXT("Ability instancing policy"), { TEXT("non_instanced"), TEXT("instanced_per_actor"), TEXT("instanced_per_execution") }));
	Schema.Add(TEXT("net_execution_policy"), FMcpSchemaProperty::MakeEnum(TEXT("Ability net execution policy"), { TEXT("local_predicted"), TEXT("local_only"), TEXT("server_initiated"), TEXT("server_only") }));
	Schema.Add(TEXT("replication_policy"), FMcpSchemaProperty::MakeEnum(TEXT("Ability replication policy"), { TEXT("replicate_no"), TEXT("replicate_yes") }));
	Schema.Add(TEXT("cost_gameplay_effect"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("GameplayEffect class or Blueprint asset path used as cost")));
	Schema.Add(TEXT("cooldown_gameplay_effect"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("GameplayEffect class or Blueprint asset path used as cooldown")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile created Blueprint")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save created Blueprint")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UCreateGameplayAbilityTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UCreateGameplayAbilityTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString ParentClassPath = GetStringArgOrDefault(Arguments, TEXT("parent_class"), TEXT("/Script/GameplayAbilities.GameplayAbility"));
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	FString Error;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, Error))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), Error);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}
	UClass* ParentClass = GASToolUtils::ResolveSubclass(ParentClassPath, UGameplayAbility::StaticClass(), Error);
	if (!ParentClass)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), Error);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	if (bDryRun)
	{
		return FMcpToolResult::StructuredJson(Response);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Gameplay Ability")));
	UBlueprint* Blueprint = GASToolUtils::CreateGameplayAbilityBlueprintAsset(AssetPath, ParentClass, Error);
	if (!Blueprint)
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), Error);
	}

	if (UGameplayAbility* AbilityCDO = GASToolUtils::GetGameplayAbilityCDO(Blueprint))
	{
		AbilityCDO->Modify();

		const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
		if (Arguments->TryGetArrayField(TEXT("ability_tags"), TagsArray))
		{
			FGameplayTagContainer Tags;
			if (!GASToolUtils::BuildTagContainer(TagsArray, Tags, Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_GAMEPLAY_TAG"), Error);
			}
			AbilityCDO->EditorGetAssetTags() = Tags;
		}

		struct FTagField
		{
			const TCHAR* JsonField;
			const TCHAR* PropertyName;
		};
		const FTagField TagFields[] = {
			{ TEXT("activation_owned_tags"), TEXT("ActivationOwnedTags") },
			{ TEXT("activation_required_tags"), TEXT("ActivationRequiredTags") },
			{ TEXT("activation_blocked_tags"), TEXT("ActivationBlockedTags") }
		};
		for (const FTagField& Field : TagFields)
		{
			if (Arguments->TryGetArrayField(Field.JsonField, TagsArray))
			{
				FGameplayTagContainer Tags;
				if (!GASToolUtils::BuildTagContainer(TagsArray, Tags, Error) || !GASToolUtils::SetTagContainerProperty(AbilityCDO, Field.PropertyName, Tags, Error))
				{
					Transaction->Cancel();
					return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_GAMEPLAY_TAG"), Error);
				}
			}
		}

		FString PolicyString;
		if (Arguments->TryGetStringField(TEXT("instancing_policy"), PolicyString))
		{
			TEnumAsByte<EGameplayAbilityInstancingPolicy::Type> Policy;
			if (!GASToolUtils::ParseAbilityInstancingPolicy(PolicyString, Policy))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported instancing_policy: %s"), *PolicyString));
			}
			if (!SetEnumByteProperty(AbilityCDO, TEXT("InstancingPolicy"), Policy.GetValue(), Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), Error);
			}
		}
		if (Arguments->TryGetStringField(TEXT("net_execution_policy"), PolicyString))
		{
			TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type> Policy;
			if (!GASToolUtils::ParseAbilityNetExecutionPolicy(PolicyString, Policy))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported net_execution_policy: %s"), *PolicyString));
			}
			if (!SetEnumByteProperty(AbilityCDO, TEXT("NetExecutionPolicy"), Policy.GetValue(), Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), Error);
			}
		}
		if (Arguments->TryGetStringField(TEXT("replication_policy"), PolicyString))
		{
			TEnumAsByte<EGameplayAbilityReplicationPolicy::Type> Policy;
			if (!GASToolUtils::ParseAbilityReplicationPolicy(PolicyString, Policy))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported replication_policy: %s"), *PolicyString));
			}
			if (!SetEnumByteProperty(AbilityCDO, TEXT("ReplicationPolicy"), Policy.GetValue(), Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), Error);
			}
		}

		FString EffectPath;
		if (Arguments->TryGetStringField(TEXT("cost_gameplay_effect"), EffectPath) && !EffectPath.IsEmpty())
		{
			UClass* EffectClass = GASToolUtils::ResolveSubclass(EffectPath, UGameplayEffect::StaticClass(), Error);
			if (!EffectClass)
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), Error);
			}
			if (!SetClassProperty(AbilityCDO, TEXT("CostGameplayEffectClass"), EffectClass, Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), Error);
			}
		}
		if (Arguments->TryGetStringField(TEXT("cooldown_gameplay_effect"), EffectPath) && !EffectPath.IsEmpty())
		{
			UClass* EffectClass = GASToolUtils::ResolveSubclass(EffectPath, UGameplayEffect::StaticClass(), Error);
			if (!EffectClass)
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), Error);
			}
			if (!SetClassProperty(AbilityCDO, TEXT("CooldownGameplayEffectClass"), EffectClass, Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), Error);
			}
		}
	}

	TSharedPtr<FJsonObject> CompileObject;
	if (!GASToolUtils::CompileAndSaveBlueprint(Blueprint, bCompile, bSave, CompileObject, Error))
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_FINALIZE_FAILED"), Error);
	}

	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayAbility(GASToolUtils::GetGameplayAbilityCDO(Blueprint), AssetPath));
	return FMcpToolResult::StructuredJson(Response);
}
