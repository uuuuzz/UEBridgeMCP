// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/EditGameplayEffectModifiersTool.h"

#include "Tools/Gameplay/GASToolUtils.h"
#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

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

FString UEditGameplayEffectModifiersTool::GetToolDescription() const
{
	return TEXT("Batch edit GameplayEffect v1 duration, period, tags, and simple constant modifiers.");
}

TMap<FString, FMcpSchemaProperty> UEditGameplayEffectModifiersTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("GameplayEffect Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("GameplayEffect edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Operation"),
		{ TEXT("set_duration"), TEXT("set_period"), TEXT("clear_modifiers"), TEXT("add_modifier"), TEXT("remove_modifier"), TEXT("set_granted_tags") },
		true)));
	OperationSchema->Properties.Add(TEXT("duration_policy"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(TEXT("Duration policy"), { TEXT("instant"), TEXT("has_duration"), TEXT("infinite") })));
	OperationSchema->Properties.Add(TEXT("duration_seconds"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Duration seconds"))));
	OperationSchema->Properties.Add(TEXT("period"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Period seconds"))));
	OperationSchema->Properties.Add(TEXT("execute_periodic_on_application"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Execute periodic effect on application"))));
	OperationSchema->Properties.Add(TEXT("index"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Modifier index for removal"))));
	OperationSchema->Properties.Add(TEXT("attribute_set_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("AttributeSet class or Blueprint asset path"))));
	OperationSchema->Properties.Add(TEXT("attribute_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Attribute property name"))));
	OperationSchema->Properties.Add(TEXT("operation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(TEXT("Modifier operation"), { TEXT("additive"), TEXT("multiplicative"), TEXT("division"), TEXT("override") })));
	OperationSchema->Properties.Add(TEXT("magnitude"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Constant scalable-float magnitude"))));
	OperationSchema->Properties.Add(TEXT("tags"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Gameplay tags"), TEXT("string"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("GameplayEffect edit operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile final Blueprint")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save final Blueprint")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UEditGameplayEffectModifiersTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditGameplayEffectModifiersTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString Error;
	UBlueprint* Blueprint = GASToolUtils::LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), Error);
	}
	UGameplayEffect* EffectCDO = GASToolUtils::GetGameplayEffectCDO(Blueprint);
	if (!EffectCDO)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"), TEXT("Blueprint is not a GameplayEffect"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit GameplayEffect Modifiers")));
		Blueprint->Modify();
		EffectCDO->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> PartialResults;
	bool bAnyFailed = false;
	bool bChanged = false;

	for (int32 Index = 0; Index < Operations->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[Index].IsValid() || !(*Operations)[Index]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), Index));
		}

		const FString Action = GetStringArgOrDefault(*OperationObject, TEXT("action"));
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("action"), Action);

		bool bSuccess = true;
		bool bOperationChanged = false;
		FString OperationError;

		if (Action == TEXT("set_duration"))
		{
			FString DurationPolicyString;
			if ((*OperationObject)->TryGetStringField(TEXT("duration_policy"), DurationPolicyString))
			{
				EGameplayEffectDurationType Policy;
				if (!GASToolUtils::ParseDurationPolicy(DurationPolicyString, Policy))
				{
					bSuccess = false;
					OperationError = FString::Printf(TEXT("Unsupported duration_policy: %s"), *DurationPolicyString);
				}
				else if (!bDryRun)
				{
					EffectCDO->DurationPolicy = Policy;
				}
				bOperationChanged = true;
			}
			float Duration = 0.0f;
			if (GetFloatArg(*OperationObject, TEXT("duration_seconds"), Duration))
			{
				if (!bDryRun)
				{
					SetMagnitude(EffectCDO->DurationMagnitude, Duration);
				}
				bOperationChanged = true;
			}
		}
		else if (Action == TEXT("set_period"))
		{
			const double Period = GetFloatArgOrDefault(*OperationObject, TEXT("period"), 0.0f);
			const bool bExecute = GetBoolArgOrDefault(*OperationObject, TEXT("execute_periodic_on_application"), EffectCDO->bExecutePeriodicEffectOnApplication);
			if (!bDryRun)
			{
				EffectCDO->Period = FScalableFloat(static_cast<float>(Period));
				EffectCDO->bExecutePeriodicEffectOnApplication = bExecute;
			}
			bOperationChanged = true;
		}
		else if (Action == TEXT("clear_modifiers"))
		{
			if (!bDryRun)
			{
				EffectCDO->Modifiers.Reset();
			}
			bOperationChanged = true;
		}
		else if (Action == TEXT("add_modifier"))
		{
			FGameplayModifierInfo Modifier;
			if (!GASToolUtils::ResolveGameplayAttribute(*OperationObject, Modifier.Attribute, OperationError))
			{
				bSuccess = false;
			}
			else
			{
				const FString Operation = GetStringArgOrDefault(*OperationObject, TEXT("operation"), TEXT("additive"));
				if (!GASToolUtils::ParseModifierOp(Operation, Modifier.ModifierOp))
				{
					bSuccess = false;
					OperationError = FString::Printf(TEXT("Unsupported modifier operation: %s"), *Operation);
				}
				else
				{
					SetMagnitude(Modifier.ModifierMagnitude, GetFloatArgOrDefault(*OperationObject, TEXT("magnitude"), 0.0f));
					if (!bDryRun)
					{
						EffectCDO->Modifiers.Add(Modifier);
					}
					bOperationChanged = true;
				}
			}
		}
		else if (Action == TEXT("remove_modifier"))
		{
			const int32 RemoveIndex = GetIntArgOrDefault(*OperationObject, TEXT("index"), -1);
			if (!EffectCDO->Modifiers.IsValidIndex(RemoveIndex))
			{
				bSuccess = false;
				OperationError = FString::Printf(TEXT("Modifier index %d is out of range"), RemoveIndex);
			}
			else
			{
				if (!bDryRun)
				{
					EffectCDO->Modifiers.RemoveAt(RemoveIndex);
				}
				bOperationChanged = true;
			}
		}
		else if (Action == TEXT("set_granted_tags"))
		{
			const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
			if (!(*OperationObject)->TryGetArrayField(TEXT("tags"), TagsArray))
			{
				bSuccess = false;
				OperationError = TEXT("'tags' is required");
			}
			else
			{
				FGameplayTagContainer Tags;
				if (!GASToolUtils::BuildTagContainer(TagsArray, Tags, OperationError))
				{
					bSuccess = false;
				}
				else if (!bDryRun && !GASToolUtils::SetGameplayEffectGrantedTags(EffectCDO, Tags, OperationError))
				{
					bSuccess = false;
				}
				else
				{
					bOperationChanged = true;
				}
			}
		}
		else
		{
			bSuccess = false;
			OperationError = FString::Printf(TEXT("Unsupported action: %s"), *Action);
		}

		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!bSuccess)
		{
			Result->SetStringField(TEXT("error"), OperationError);
			bAnyFailed = true;
			PartialResults.Add(MakeShareable(new FJsonValueObject(Result)));
			if (bRollbackOnError && !bDryRun)
			{
				Transaction->Cancel();
				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("asset_path"), AssetPath);
				Details->SetNumberField(TEXT("failed_action_index"), Index);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), OperationError, Details);
			}
		}
		bChanged = bChanged || bOperationChanged;
		Results.Add(MakeShareable(new FJsonValueObject(Result)));
	}

	TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
	if (!bDryRun && bChanged)
	{
		if (!GASToolUtils::CompileAndSaveBlueprint(Blueprint, bCompile, bSave, CompileObject, Error))
		{
			if (bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_FINALIZE_FAILED"), Error);
		}
	}
	else
	{
		CompileObject->SetBoolField(TEXT("attempted"), false);
		CompileObject->SetBoolField(TEXT("success"), true);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetArrayField(TEXT("results"), Results);
	Response->SetArrayField(TEXT("partial_results"), PartialResults);
	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayEffect(EffectCDO, AssetPath));
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
