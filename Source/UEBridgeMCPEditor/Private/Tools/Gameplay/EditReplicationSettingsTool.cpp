// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/EditReplicationSettingsTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"

#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

namespace
{
	bool SetPropertyByPath(UObject* TargetObject, const FString& PropertyPath, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		FProperty* Property = nullptr;
		void* Container = nullptr;
		if (!FMcpAssetModifier::FindPropertyByPath(TargetObject, PropertyPath, Property, Container, OutError))
		{
			return false;
		}

		return FMcpPropertySerializer::DeserializePropertyValue(Property, Container, Value, OutError);
	}

	FBPVariableDescription* FindBlueprintVariable(UBlueprint* Blueprint, const FString& VariableName)
	{
		for (FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName.ToString().Equals(VariableName, ESearchCase::IgnoreCase))
			{
				return &Variable;
			}
		}
		return nullptr;
	}
}

FString UEditReplicationSettingsTool::GetToolDescription() const
{
	return TEXT("Edit Blueprint replication settings for actor defaults and Blueprint-authored variables.");
}

TMap<FString, FMcpSchemaProperty> UEditReplicationSettingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Replication edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Replication action"),
		{ TEXT("set_class_settings"), TEXT("set_variable_replication") },
		true)));
	OperationSchema->Properties.Add(TEXT("variable_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint variable name"))));
	OperationSchema->Properties.Add(TEXT("replication"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Variable replication mode"),
		{ TEXT("none"), TEXT("replicated"), TEXT("repnotify") })));
	OperationSchema->Properties.Add(TEXT("replicated_using"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("RepNotify function name"))));
	OperationSchema->Properties.Add(TEXT("replicates"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Actor replicates"))));
	OperationSchema->Properties.Add(TEXT("replicate_movement"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Actor replicates movement"))));
	OperationSchema->Properties.Add(TEXT("always_relevant"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Actor is always relevant"))));
	OperationSchema->Properties.Add(TEXT("only_relevant_to_owner"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Actor only relevant to owner"))));
	OperationSchema->Properties.Add(TEXT("use_owner_relevancy"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Use owner relevancy"))));
	OperationSchema->Properties.Add(TEXT("net_cull_distance_squared"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Net cull distance squared"))));
	OperationSchema->Properties.Add(TEXT("net_update_frequency"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Net update frequency"))));
	OperationSchema->Properties.Add(TEXT("min_net_update_frequency"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Minimum net update frequency"))));
	OperationSchema->Properties.Add(TEXT("dormancy"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Dormancy enum name, e.g. 'DORM_Never'"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Replication edit operations");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the Blueprint asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UEditReplicationSettingsTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditReplicationSettingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Replication Settings")));
		Blueprint->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bChanged = false;
	bool bNeedsCompile = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString ActionName;
		(*OperationObject)->TryGetStringField(TEXT("action"), ActionName);
		ResultObject->SetStringField(TEXT("action"), ActionName);

		bool bOperationSuccess = false;
		bool bOperationChanged = false;
		FString OperationError;

		if (ActionName == TEXT("set_class_settings"))
		{
			UObject* DefaultObject = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
			if (!DefaultObject || !DefaultObject->IsA<AActor>())
			{
				OperationError = TEXT("Blueprint does not expose an actor default object");
			}
			else
			{
				struct FPropertyFieldInfo
				{
					const TCHAR* JsonField;
					const TCHAR* PropertyPath;
				};

				const FPropertyFieldInfo Fields[] = {
					{ TEXT("replicates"), TEXT("bReplicates") },
					{ TEXT("replicate_movement"), TEXT("bReplicateMovement") },
					{ TEXT("always_relevant"), TEXT("bAlwaysRelevant") },
					{ TEXT("only_relevant_to_owner"), TEXT("bOnlyRelevantToOwner") },
					{ TEXT("use_owner_relevancy"), TEXT("bNetUseOwnerRelevancy") },
					{ TEXT("net_cull_distance_squared"), TEXT("NetCullDistanceSquared") },
					{ TEXT("net_update_frequency"), TEXT("NetUpdateFrequency") },
					{ TEXT("min_net_update_frequency"), TEXT("MinNetUpdateFrequency") },
					{ TEXT("dormancy"), TEXT("NetDormancy") }
				};

				for (const FPropertyFieldInfo& FieldInfo : Fields)
				{
					if (TSharedPtr<FJsonValue> Value = (*OperationObject)->TryGetField(FieldInfo.JsonField))
					{
						bOperationChanged = true;
						if (!bDryRun && !SetPropertyByPath(DefaultObject, FieldInfo.PropertyPath, Value, OperationError))
						{
							break;
						}
					}
				}

				if (OperationError.IsEmpty())
				{
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("set_variable_replication"))
		{
			FString VariableName;
			if (!(*OperationObject)->TryGetStringField(TEXT("variable_name"), VariableName) || VariableName.IsEmpty())
			{
				OperationError = TEXT("'variable_name' is required for set_variable_replication");
			}
			else
			{
				FBPVariableDescription* Variable = FindBlueprintVariable(Blueprint, VariableName);
				if (!Variable)
				{
					OperationError = TEXT("Blueprint variable not found");
				}
				else
				{
					FString ReplicationMode;
					if (!(*OperationObject)->TryGetStringField(TEXT("replication"), ReplicationMode) || ReplicationMode.IsEmpty())
					{
						OperationError = TEXT("'replication' is required for set_variable_replication");
					}
					else
					{
						bOperationChanged = true;
						if (!bDryRun)
						{
							Variable->PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
							Variable->RepNotifyFunc = NAME_None;

							if (ReplicationMode.Equals(TEXT("replicated"), ESearchCase::IgnoreCase))
							{
								Variable->PropertyFlags |= CPF_Net;
							}
							else if (ReplicationMode.Equals(TEXT("repnotify"), ESearchCase::IgnoreCase))
							{
								FString RepNotifyFunctionName;
								if (!(*OperationObject)->TryGetStringField(TEXT("replicated_using"), RepNotifyFunctionName) || RepNotifyFunctionName.IsEmpty())
								{
									RepNotifyFunctionName = FString::Printf(TEXT("OnRep_%s"), *Variable->VarName.ToString());
								}
								Variable->PropertyFlags |= (CPF_Net | CPF_RepNotify);
								Variable->RepNotifyFunc = FName(*RepNotifyFunctionName);
								ResultObject->SetStringField(TEXT("replicated_using"), RepNotifyFunctionName);
							}
							else if (!ReplicationMode.Equals(TEXT("none"), ESearchCase::IgnoreCase))
							{
								OperationError = FString::Printf(TEXT("Unsupported replication mode '%s'"), *ReplicationMode);
							}
						}

						if (OperationError.IsEmpty())
						{
							ResultObject->SetStringField(TEXT("variable_name"), VariableName);
							ResultObject->SetStringField(TEXT("replication"), ReplicationMode);
							bNeedsCompile = true;
							bOperationSuccess = true;
						}
					}
				}
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!bOperationSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), OperationError);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					OperationError,
					nullptr,
					GameplayToolUtils::BuildBatchFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}
		else
		{
			bChanged = bChanged || bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (bNeedsCompile)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);
			FString CompileError;
			if (!FMcpAssetModifier::CompileBlueprint(Blueprint, CompileError))
			{
				if (Transaction.IsValid() && bRollbackOnError)
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
			}
		}

		FMcpAssetModifier::MarkPackageDirty(Blueprint);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && bSave && bChanged)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
