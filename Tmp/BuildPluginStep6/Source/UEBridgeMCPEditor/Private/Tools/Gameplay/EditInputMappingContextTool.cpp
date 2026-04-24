// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/EditInputMappingContextTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "ScopedTransaction.h"

namespace
{
	int32 FindMappingIndex(const UInputMappingContext* MappingContext, const FString& ActionAssetPath, const FString& KeyName)
	{
		const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
		for (int32 Index = 0; Index < Mappings.Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Mapping = Mappings[Index];
			const bool bActionMatches = Mapping.Action && Mapping.Action->GetPathName().Equals(ActionAssetPath, ESearchCase::IgnoreCase);
			const bool bKeyMatches = KeyName.IsEmpty() || Mapping.Key.ToString().Equals(KeyName, ESearchCase::IgnoreCase);
			if (bActionMatches && bKeyMatches)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

FString UEditInputMappingContextTool::GetToolDescription() const
{
	return TEXT("Edit an Enhanced Input Mapping Context with batched mapping operations.");
}

TMap<FString, FMcpSchemaProperty> UEditInputMappingContextTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input Mapping Context asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Input Mapping Context operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Mapping context edit action"),
		{ TEXT("add_mapping"), TEXT("remove_mapping"), TEXT("replace_mapping_key"), TEXT("clear_mappings"), TEXT("set_context_description") },
		true)));
	OperationSchema->Properties.Add(TEXT("action_asset_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input Action asset path"))));
	OperationSchema->Properties.Add(TEXT("key"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input key"))));
	OperationSchema->Properties.Add(TEXT("new_key"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Replacement input key"))));
	OperationSchema->Properties.Add(TEXT("description"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New context description"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Input Mapping Context operations");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the mapping context asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UEditInputMappingContextTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditInputMappingContextTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
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
	UInputMappingContext* MappingContext = FMcpAssetModifier::LoadAssetByPath<UInputMappingContext>(AssetPath, LoadError);
	if (!MappingContext)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Input Mapping Context")));
		MappingContext->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bChanged = false;

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

		if (ActionName == TEXT("add_mapping"))
		{
			FString ActionAssetPath;
			FString KeyName;
			if (!(*OperationObject)->TryGetStringField(TEXT("action_asset_path"), ActionAssetPath) || !(*OperationObject)->TryGetStringField(TEXT("key"), KeyName))
			{
				OperationError = TEXT("'action_asset_path' and 'key' are required for add_mapping");
			}
			else
			{
				FString KeyError;
				FKey ParsedKey;
				if (!GameplayToolUtils::ParseKey(KeyName, ParsedKey, KeyError))
				{
					OperationError = KeyError;
				}
				else
				{
					FString ActionLoadError;
					UInputAction* InputAction = FMcpAssetModifier::LoadAssetByPath<UInputAction>(ActionAssetPath, ActionLoadError);
					if (!InputAction)
					{
						OperationError = ActionLoadError;
					}
					else
					{
						bOperationChanged = FindMappingIndex(MappingContext, ActionAssetPath, KeyName) == INDEX_NONE;
						if (!bDryRun && bOperationChanged)
						{
							MappingContext->MapKey(InputAction, ParsedKey);
						}
						ResultObject->SetStringField(TEXT("action_asset_path"), ActionAssetPath);
						ResultObject->SetStringField(TEXT("key"), KeyName);
						bOperationSuccess = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("remove_mapping"))
		{
			FString ActionAssetPath;
			if (!(*OperationObject)->TryGetStringField(TEXT("action_asset_path"), ActionAssetPath))
			{
				OperationError = TEXT("'action_asset_path' is required for remove_mapping");
			}
			else
			{
				const FString KeyName = GetStringArgOrDefault(*OperationObject, TEXT("key"));
				const int32 MappingIndex = FindMappingIndex(MappingContext, ActionAssetPath, KeyName);
				bOperationChanged = MappingIndex != INDEX_NONE;
				if (!bDryRun && bOperationChanged)
				{
					FString ActionLoadError;
					UInputAction* InputAction = FMcpAssetModifier::LoadAssetByPath<UInputAction>(ActionAssetPath, ActionLoadError);
					if (!InputAction)
					{
						OperationError = ActionLoadError;
					}
					else if (KeyName.IsEmpty())
					{
						MappingContext->UnmapAllKeysFromAction(InputAction);
					}
					else
					{
						FString KeyError;
						FKey ParsedKey;
						if (!GameplayToolUtils::ParseKey(KeyName, ParsedKey, KeyError))
						{
							OperationError = KeyError;
						}
						else
						{
							MappingContext->UnmapKey(InputAction, ParsedKey);
						}
					}
				}

				if (OperationError.IsEmpty())
				{
					ResultObject->SetStringField(TEXT("action_asset_path"), ActionAssetPath);
					if (!KeyName.IsEmpty())
					{
						ResultObject->SetStringField(TEXT("key"), KeyName);
					}
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("replace_mapping_key"))
		{
			FString ActionAssetPath;
			FString KeyName;
			FString NewKeyName;
			if (!(*OperationObject)->TryGetStringField(TEXT("action_asset_path"), ActionAssetPath)
				|| !(*OperationObject)->TryGetStringField(TEXT("key"), KeyName)
				|| !(*OperationObject)->TryGetStringField(TEXT("new_key"), NewKeyName))
			{
				OperationError = TEXT("'action_asset_path', 'key', and 'new_key' are required for replace_mapping_key");
			}
			else
			{
				const int32 MappingIndex = FindMappingIndex(MappingContext, ActionAssetPath, KeyName);
				if (MappingIndex == INDEX_NONE)
				{
					OperationError = TEXT("Mapping not found");
				}
				else
				{
					FString KeyError;
					FKey ParsedKey;
					if (!GameplayToolUtils::ParseKey(NewKeyName, ParsedKey, KeyError))
					{
						OperationError = KeyError;
					}
					else
					{
						bOperationChanged = !KeyName.Equals(NewKeyName, ESearchCase::IgnoreCase);
						if (!bDryRun && bOperationChanged)
						{
							MappingContext->GetMapping(MappingIndex).Key = ParsedKey;
						}
						ResultObject->SetStringField(TEXT("action_asset_path"), ActionAssetPath);
						ResultObject->SetStringField(TEXT("key"), KeyName);
						ResultObject->SetStringField(TEXT("new_key"), NewKeyName);
						bOperationSuccess = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("clear_mappings"))
		{
			bOperationChanged = MappingContext->GetMappings().Num() > 0;
			if (!bDryRun && bOperationChanged)
			{
				MappingContext->UnmapAll();
			}
			bOperationSuccess = true;
		}
		else if (ActionName == TEXT("set_context_description"))
		{
			FString Description;
			if (!(*OperationObject)->TryGetStringField(TEXT("description"), Description))
			{
				OperationError = TEXT("'description' is required for set_context_description");
			}
			else
			{
				bOperationChanged = !MappingContext->ContextDescription.ToString().Equals(Description, ESearchCase::CaseSensitive);
				if (!bDryRun && bOperationChanged)
				{
					MappingContext->ContextDescription = FText::FromString(Description);
				}
				ResultObject->SetStringField(TEXT("description"), Description);
				bOperationSuccess = true;
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
		FMcpAssetModifier::MarkPackageDirty(MappingContext);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && bSave && bChanged)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(MappingContext, false, SaveError))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TArray<TSharedPtr<FJsonValue>> MappingArray;
	for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
	{
		MappingArray.Add(MakeShareable(new FJsonValueObject(GameplayToolUtils::SerializeInputMapping(Mapping))));
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
	Response->SetArrayField(TEXT("mappings"), MappingArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
