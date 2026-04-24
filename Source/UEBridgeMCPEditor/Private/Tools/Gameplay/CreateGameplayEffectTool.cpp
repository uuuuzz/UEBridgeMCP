// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateGameplayEffectTool.h"

#include "Tools/Gameplay/GASToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "ScopedTransaction.h"
#include "ScalableFloat.h"

namespace
{
	void SetMagnitude(FGameplayEffectModifierMagnitude& Target, float Value)
	{
		Target = FGameplayEffectModifierMagnitude(FScalableFloat(Value));
	}
}

FString UCreateGameplayEffectTool::GetToolDescription() const
{
	return TEXT("Create a GameplayEffect Blueprint and configure v1 duration, period, granted tags, and simple constant modifiers.");
}

TMap<FString, FMcpSchemaProperty> UCreateGameplayEffectTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("GameplayEffect Blueprint asset path"), true));
	Schema.Add(TEXT("parent_class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional parent class path; defaults to /Script/GameplayAbilities.GameplayEffect")));
	Schema.Add(TEXT("duration_policy"), FMcpSchemaProperty::MakeEnum(TEXT("Duration policy"), { TEXT("instant"), TEXT("has_duration"), TEXT("infinite") }));
	Schema.Add(TEXT("duration_seconds"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Duration seconds for has_duration effects")));
	Schema.Add(TEXT("period"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional periodic tick period")));
	Schema.Add(TEXT("execute_periodic_on_application"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Execute periodic effect on application")));
	Schema.Add(TEXT("granted_tags"), FMcpSchemaProperty::MakeArray(TEXT("Deprecated monolithic granted tags for v1 smoke use"), TEXT("string")));
	Schema.Add(TEXT("modifiers"), FMcpSchemaProperty::MakeArray(TEXT("Initial modifier objects with attribute_set_class, attribute_name, operation, magnitude"), TEXT("object")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile created Blueprint")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save created Blueprint")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UCreateGameplayEffectTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UCreateGameplayEffectTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString ParentClassPath = GetStringArgOrDefault(Arguments, TEXT("parent_class"), TEXT("/Script/GameplayAbilities.GameplayEffect"));
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
	UClass* ParentClass = GASToolUtils::ResolveSubclass(ParentClassPath, UGameplayEffect::StaticClass(), Error);
	if (!ParentClass)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), Error);
	}

	const TArray<TSharedPtr<FJsonValue>>* ModifierArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("modifiers"), ModifierArray) && ModifierArray)
	{
		for (int32 Index = 0; Index < ModifierArray->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* ModifierObject = nullptr;
			if (!(*ModifierArray)[Index].IsValid() || !(*ModifierArray)[Index]->TryGetObject(ModifierObject) || !ModifierObject || !(*ModifierObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("modifiers[%d] must be an object"), Index));
			}
			FGameplayAttribute Attribute;
			if (!GASToolUtils::ResolveGameplayAttribute(*ModifierObject, Attribute, Error))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ATTRIBUTE"), Error);
			}
		}
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

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Gameplay Effect")));
	UBlueprint* Blueprint = GASToolUtils::CreateGASBlueprintAsset(AssetPath, ParentClass, Error);
	if (!Blueprint)
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), Error);
	}

	UGameplayEffect* EffectCDO = GASToolUtils::GetGameplayEffectCDO(Blueprint);
	if (!EffectCDO)
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"), TEXT("Created Blueprint does not expose a GameplayEffect CDO"));
	}

	EffectCDO->Modify();
	FString DurationPolicyString;
	if (Arguments->TryGetStringField(TEXT("duration_policy"), DurationPolicyString))
	{
		EGameplayEffectDurationType Policy;
		if (!GASToolUtils::ParseDurationPolicy(DurationPolicyString, Policy))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported duration_policy: %s"), *DurationPolicyString));
		}
		EffectCDO->DurationPolicy = Policy;
	}
	double NumberValue = 0.0;
	if (Arguments->TryGetNumberField(TEXT("duration_seconds"), NumberValue))
	{
		SetMagnitude(EffectCDO->DurationMagnitude, static_cast<float>(NumberValue));
	}
	if (Arguments->TryGetNumberField(TEXT("period"), NumberValue))
	{
		EffectCDO->Period = FScalableFloat(static_cast<float>(NumberValue));
	}
	bool bBoolValue = false;
	if (Arguments->TryGetBoolField(TEXT("execute_periodic_on_application"), bBoolValue))
	{
		EffectCDO->bExecutePeriodicEffectOnApplication = bBoolValue;
	}

	const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("granted_tags"), TagsArray))
	{
		FGameplayTagContainer Tags;
		if (!GASToolUtils::BuildTagContainer(TagsArray, Tags, Error) ||
			!GASToolUtils::SetGameplayEffectGrantedTags(EffectCDO, Tags, Error))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_GAMEPLAY_TAG"), Error);
		}
	}

	if (ModifierArray)
	{
		EffectCDO->Modifiers.Reset();
		for (const TSharedPtr<FJsonValue>& ModifierValue : *ModifierArray)
		{
			const TSharedPtr<FJsonObject>* ModifierObject = nullptr;
			ModifierValue->TryGetObject(ModifierObject);
			FGameplayModifierInfo Modifier;
			if (!GASToolUtils::ResolveGameplayAttribute(*ModifierObject, Modifier.Attribute, Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ATTRIBUTE"), Error);
			}
			FString Operation = GetStringArgOrDefault(*ModifierObject, TEXT("operation"), TEXT("additive"));
			if (!GASToolUtils::ParseModifierOp(Operation, Modifier.ModifierOp))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported modifier operation: %s"), *Operation));
			}
			const double Magnitude = GetFloatArgOrDefault(*ModifierObject, TEXT("magnitude"), 0.0f);
			SetMagnitude(Modifier.ModifierMagnitude, static_cast<float>(Magnitude));
			EffectCDO->Modifiers.Add(Modifier);
		}
	}

	TSharedPtr<FJsonObject> CompileObject;
	if (!GASToolUtils::CompileAndSaveBlueprint(Blueprint, bCompile, bSave, CompileObject, Error))
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_FINALIZE_FAILED"), Error);
	}

	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayEffect(GASToolUtils::GetGameplayEffectCDO(Blueprint), AssetPath));
	return FMcpToolResult::StructuredJson(Response);
}
