// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/ManageAbilitySystemBindingsTool.h"

#include "Tools/Gameplay/GASToolUtils.h"
#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

FString UManageAbilitySystemBindingsTool::GetToolDescription() const
{
	return TEXT("Add AbilitySystemComponent setup and basic GAS class-reference bindings to an Actor Blueprint.");
}

TMap<FString, FMcpSchemaProperty> UManageAbilitySystemBindingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> ActionSchema = MakeShared<FMcpSchemaProperty>();
	ActionSchema->Type = TEXT("object");
	ActionSchema->Description = TEXT("Ability system binding action");
	ActionSchema->NestedRequired = { TEXT("action") };
	ActionSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Binding action"),
		{ TEXT("add_ability_system_component"), TEXT("bind_ability_class"), TEXT("bind_gameplay_effect_class"), TEXT("bind_attribute_set_class"), TEXT("remove_binding_variable") },
		true)));
	ActionSchema->Properties.Add(TEXT("component_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("ASC component name; default AbilitySystem"))));
	ActionSchema->Properties.Add(TEXT("replication_mode"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(TEXT("ASC replication mode"), { TEXT("minimal"), TEXT("mixed"), TEXT("full") })));
	ActionSchema->Properties.Add(TEXT("class_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Ability, effect, or attribute-set class or Blueprint asset path"))));
	ActionSchema->Properties.Add(TEXT("variable_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional binding variable name"))));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Binding actions");
	ActionsSchema.bRequired = true;
	ActionsSchema.Items = ActionSchema;
	Schema.Add(TEXT("actions"), ActionsSchema);
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile final Blueprint")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save final Blueprint")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UManageAbilitySystemBindingsTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("actions") };
}

FMcpToolResult UManageAbilitySystemBindingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), Actions) || !Actions || Actions->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	FString Error;
	UBlueprint* Blueprint = GASToolUtils::LoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), Error);
	}
	if (!Blueprint->SimpleConstructionScript)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"), TEXT("Blueprint is not an Actor Blueprint with a SimpleConstructionScript"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Manage Ability System Bindings")));
		Blueprint->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> PartialResults;
	bool bAnyFailed = false;
	bool bChanged = false;

	for (int32 Index = 0; Index < Actions->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!(*Actions)[Index].IsValid() || !(*Actions)[Index]->TryGetObject(ActionObject) || !ActionObject || !(*ActionObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("actions[%d] must be an object"), Index));
		}

		const FString Action = GetStringArgOrDefault(*ActionObject, TEXT("action"));
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("action"), Action);

		bool bSuccess = true;
		bool bOperationChanged = true;
		FString OperationError;

		if (Action == TEXT("add_ability_system_component"))
		{
			Result->SetStringField(TEXT("component_name"), GetStringArgOrDefault(*ActionObject, TEXT("component_name"), TEXT("AbilitySystem")));
			if (!bDryRun)
			{
				bSuccess = GASToolUtils::AddAbilitySystemComponent(
					Blueprint,
					GetStringArgOrDefault(*ActionObject, TEXT("component_name"), TEXT("AbilitySystem")),
					GetStringArgOrDefault(*ActionObject, TEXT("replication_mode"), TEXT("mixed")),
					Result,
					OperationError);
			}
		}
		else if (Action == TEXT("bind_ability_class") || Action == TEXT("bind_gameplay_effect_class") || Action == TEXT("bind_attribute_set_class"))
		{
			const FString ClassPath = GetStringArgOrDefault(*ActionObject, TEXT("class_path"));
			if (ClassPath.IsEmpty())
			{
				bSuccess = false;
				OperationError = TEXT("'class_path' is required");
			}
			else
			{
				UClass* BaseClass = UGameplayAbility::StaticClass();
				FString BindingKind = TEXT("ability");
				if (Action == TEXT("bind_gameplay_effect_class"))
				{
					BaseClass = UGameplayEffect::StaticClass();
					BindingKind = TEXT("gameplay_effect");
				}
				else if (Action == TEXT("bind_attribute_set_class"))
				{
					BaseClass = UAttributeSet::StaticClass();
					BindingKind = TEXT("attribute_set");
				}

				UClass* BoundClass = GASToolUtils::ResolveSubclass(ClassPath, BaseClass, OperationError);
				if (!BoundClass)
				{
					bSuccess = false;
				}
				else
				{
					Result->SetStringField(TEXT("bound_class"), BoundClass->GetPathName());
					if (!bDryRun)
					{
						bSuccess = GASToolUtils::AddClassBindingVariable(
							Blueprint,
							GetStringArgOrDefault(*ActionObject, TEXT("variable_name")),
							BaseClass,
							BoundClass,
							BindingKind,
							Result,
							OperationError);
					}
				}
			}
		}
		else if (Action == TEXT("remove_binding_variable"))
		{
			const FString VariableName = GetStringArgOrDefault(*ActionObject, TEXT("variable_name"));
			if (VariableName.IsEmpty())
			{
				bSuccess = false;
				OperationError = TEXT("'variable_name' is required");
			}
			else
			{
				Result->SetStringField(TEXT("variable_name"), VariableName);
				if (!bDryRun)
				{
					if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VariableName)) == INDEX_NONE)
					{
						bSuccess = false;
						OperationError = FString::Printf(TEXT("Binding variable not found: %s"), *VariableName);
					}
					else
					{
						FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VariableName));
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					}
				}
			}
		}
		else
		{
			bSuccess = false;
			OperationError = FString::Printf(TEXT("Unsupported action: %s"), *Action);
		}

		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetBoolField(TEXT("changed"), bSuccess && bOperationChanged);
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
		bChanged = bChanged || (bSuccess && bOperationChanged);
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
	Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeActorGASBindings(Blueprint, AssetPath));
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
