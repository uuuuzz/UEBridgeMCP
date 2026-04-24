// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Niagara/EditNiagaraUserParametersTool.h"

#include "Tools/Niagara/NiagaraToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FJsonObject> BuildFailurePayload(
		const TArray<TSharedPtr<FJsonValue>>& Results,
		const TArray<TSharedPtr<FJsonValue>>& Warnings,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssets)
	{
		TSharedPtr<FJsonObject> Partial = MakeShareable(new FJsonObject);
		Partial->SetStringField(TEXT("tool"), TEXT("edit-niagara-user-parameters"));
		Partial->SetArrayField(TEXT("results"), Results);
		Partial->SetArrayField(TEXT("warnings"), Warnings);
		Partial->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
		return Partial;
	}
}

FString UEditNiagaraUserParametersTool::GetToolDescription() const
{
	return TEXT("Batch edit exposed Niagara user parameters on a system asset. Supports add, remove, rename, and set_default for bool/int/float/vector/color values.");
}

TMap<FString, FMcpSchemaProperty> UEditNiagaraUserParametersTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Niagara system asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Niagara user parameter operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("User parameter action"),
		{ TEXT("add_parameter"), TEXT("remove_parameter"), TEXT("rename_parameter"), TEXT("set_default") },
		true)));
	OperationSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Parameter name, with or without User. prefix"))));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New parameter name for rename_parameter"))));
	OperationSchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Parameter type"),
		{ TEXT("bool"), TEXT("int32"), TEXT("float"), TEXT("vector2"), TEXT("vector3"), TEXT("position"), TEXT("vector4"), TEXT("color") })));
	OperationSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Default value; scalars may be scalar JSON values, vectors may be arrays or objects"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("User parameter operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only without mutating the asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on first failure")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Request a final Niagara compile after successful edits")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the Niagara system asset after successful edits")));
	return Schema;
}

FMcpToolResult UEditNiagaraUserParametersTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UNiagaraSystem* System = FMcpAssetModifier::LoadAssetByPath<UNiagaraSystem>(AssetPath, LoadError);
	if (!System)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Niagara User Parameters")));
		System->Modify();
	}

	FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	bool bAnyFailed = false;
	bool bAnyChanged = false;
	int32 Succeeded = 0;
	int32 Failed = 0;

	for (int32 Index = 0; Index < Operations->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Operation = nullptr;
		if (!(*Operations)[Index].IsValid() || !(*Operations)[Index]->TryGetObject(Operation) || !Operation || !(*Operation).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), Index));
		}

		FString Action;
		(*Operation)->TryGetStringField(TEXT("action"), Action);
		FString Name;
		(*Operation)->TryGetStringField(TEXT("name"), Name);

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("name"), Name);

		bool bSuccess = false;
		bool bChanged = false;
		FString Error;

		if (Name.IsEmpty() && Action != TEXT("remove_parameter"))
		{
			Error = TEXT("'name' is required");
		}
		else if (Action == TEXT("add_parameter"))
		{
			FString TypeName;
			if (!(*Operation)->TryGetStringField(TEXT("type"), TypeName))
			{
				Error = TEXT("'type' is required for add_parameter");
			}
			else
			{
				FNiagaraTypeDefinition TypeDefinition;
				if (NiagaraToolUtils::TryResolveType(TypeName, TypeDefinition, Error))
				{
					FNiagaraVariable Variable(TypeDefinition, FName(*NiagaraToolUtils::NormalizeUserParameterName(Name)));
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(Variable);

					FNiagaraVariable Existing;
					const bool bAlreadyExists = NiagaraToolUtils::TryFindUserParameter(Store, Name, Existing);
					if (!bDryRun && !bAlreadyExists)
					{
						Store.AddParameter(Variable, true, true);
					}
					bSuccess = true;
					bChanged = !bAlreadyExists;
					Result->SetStringField(TEXT("type"), NiagaraToolUtils::TypeToString(TypeDefinition));
					Result->SetBoolField(TEXT("already_exists"), bAlreadyExists);

					const TSharedPtr<FJsonValue> DefaultValue = (*Operation)->TryGetField(TEXT("value"));
					if (DefaultValue.IsValid())
					{
						if (!bDryRun && !NiagaraToolUtils::SetParameterStoreValue(Store, Variable, DefaultValue, Error))
						{
							bSuccess = false;
						}
					}
				}
			}
		}
		else if (Action == TEXT("remove_parameter"))
		{
			if (Name.IsEmpty())
			{
				Error = TEXT("'name' is required for remove_parameter");
			}
			else
			{
				FNiagaraVariable Existing;
				if (!NiagaraToolUtils::TryFindUserParameter(Store, Name, Existing))
				{
					Error = FString::Printf(TEXT("User parameter not found: %s"), *Name);
				}
				else
				{
					if (!bDryRun)
					{
						Store.RemoveParameter(Existing);
					}
					bSuccess = true;
					bChanged = true;
					Result->SetStringField(TEXT("type"), NiagaraToolUtils::TypeToString(Existing.GetType()));
				}
			}
		}
		else if (Action == TEXT("rename_parameter"))
		{
			FString NewName;
			if (!(*Operation)->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
			{
				Error = TEXT("'new_name' is required for rename_parameter");
			}
			else
			{
				FNiagaraVariable Existing;
				if (!NiagaraToolUtils::TryFindUserParameter(Store, Name, Existing))
				{
					Error = FString::Printf(TEXT("User parameter not found: %s"), *Name);
				}
				else
				{
					if (!bDryRun)
					{
						Store.RenameParameter(Existing, FName(*NiagaraToolUtils::NormalizeUserParameterName(NewName)));
					}
					bSuccess = true;
					bChanged = true;
					Result->SetStringField(TEXT("new_name"), NewName);
					Result->SetStringField(TEXT("type"), NiagaraToolUtils::TypeToString(Existing.GetType()));
				}
			}
		}
		else if (Action == TEXT("set_default"))
		{
			const TSharedPtr<FJsonValue> Value = (*Operation)->TryGetField(TEXT("value"));
			if (!Value.IsValid())
			{
				Error = TEXT("'value' is required for set_default");
			}
			else
			{
				FNiagaraVariable Variable;
				if (!NiagaraToolUtils::TryFindUserParameter(Store, Name, Variable))
				{
					FString TypeName;
					if (!(*Operation)->TryGetStringField(TEXT("type"), TypeName))
					{
						Error = FString::Printf(TEXT("User parameter not found: %s. Provide 'type' to create it while setting a default."), *Name);
					}
					else
					{
						FNiagaraTypeDefinition TypeDefinition;
						if (NiagaraToolUtils::TryResolveType(TypeName, TypeDefinition, Error))
						{
							Variable = FNiagaraVariable(TypeDefinition, FName(*NiagaraToolUtils::NormalizeUserParameterName(Name)));
							FNiagaraUserRedirectionParameterStore::MakeUserVariable(Variable);
							if (!bDryRun)
							{
								Store.AddParameter(Variable, true, true);
							}
							bChanged = true;
						}
					}
				}

				if (Error.IsEmpty())
				{
					if (!bDryRun && !NiagaraToolUtils::SetParameterStoreValue(Store, Variable, Value, Error))
					{
						bSuccess = false;
					}
					else
					{
						bSuccess = true;
						bChanged = true;
						Result->SetStringField(TEXT("type"), NiagaraToolUtils::TypeToString(Variable.GetType()));
					}
				}
			}
		}
		else
		{
			Error = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetBoolField(TEXT("changed"), bChanged);
		if (!Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), Error);
		}

		Results.Add(MakeShareable(new FJsonValueObject(Result)));

		if (bSuccess)
		{
			++Succeeded;
			bAnyChanged = bAnyChanged || bChanged;
		}
		else
		{
			++Failed;
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_INVALID_ACTION"),
					Error.IsEmpty() ? TEXT("Niagara user parameter operation failed") : Error,
					nullptr,
					BuildFailurePayload(Results, Warnings, ModifiedAssets));
			}
		}
	}

	TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
	CompileObject->SetBoolField(TEXT("requested"), false);
	if (!bDryRun && bAnyChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(System);
		ModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
		if (bCompile)
		{
			CompileObject = NiagaraToolUtils::CompileSystem(System, true);
		}
		if (bSave)
		{
			NiagaraToolUtils::SaveAsset(System, Warnings);
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), Operations->Num());
	Summary->SetNumberField(TEXT("succeeded"), Succeeded);
	Summary->SetNumberField(TEXT("failed"), Failed);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), Results);
	Response->SetArrayField(TEXT("warnings"), Warnings);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetArrayField(TEXT("user_parameters"), NiagaraToolUtils::SerializeUserParameters(Store));
	Response->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
