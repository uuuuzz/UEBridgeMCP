// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/EditDataTableBatchTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpDataTableUtils.h"

#include "Engine/DataTable.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FJsonObject> BuildPartialFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> PartialObject = MakeShareable(new FJsonObject);
		PartialObject->SetStringField(TEXT("tool"), ToolName);
		PartialObject->SetArrayField(TEXT("results"), ResultsArray);
		PartialObject->SetArrayField(TEXT("warnings"), WarningsArray);
		PartialObject->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		PartialObject->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		PartialObject->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return PartialObject;
	}

	bool CopyRowToBuffer(const UScriptStruct* RowStruct, const uint8* SourceRow, TArray<uint8>& OutBuffer, FString& OutError)
	{
		if (!McpDataTableUtils::CreateInitializedRowBuffer(RowStruct, OutBuffer, OutError))
		{
			return false;
		}
		RowStruct->CopyScriptStruct(OutBuffer.GetData(), SourceRow);
		return true;
	}

	TSharedPtr<FJsonObject> GetFieldsObjectOrEmpty(const TSharedPtr<FJsonObject>& OperationObject)
	{
		const TSharedPtr<FJsonObject>* FieldsObject = nullptr;
		if (OperationObject->TryGetObjectField(TEXT("fields"), FieldsObject) && FieldsObject && (*FieldsObject).IsValid())
		{
			return *FieldsObject;
		}
		return MakeShareable(new FJsonObject);
	}

	void AppendWarnings(const TArray<FString>& InWarnings, TArray<TSharedPtr<FJsonValue>>& OutWarningsArray)
	{
		for (const FString& Warning : InWarnings)
		{
			OutWarningsArray.Add(MakeShareable(new FJsonValueString(Warning)));
		}
	}
}

FString UEditDataTableBatchTool::GetToolDescription() const
{
	return TEXT("Batch edit a DataTable with structured row operations such as upsert, set_fields, delete, rename, and bulk import.");
}

TMap<FString, FMcpSchemaProperty> UEditDataTableBatchTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("DataTable asset path"), true));

	TSharedPtr<FJsonObject> AdditionalPropsSchema = MakeShareable(new FJsonObject);
	AdditionalPropsSchema->SetStringField(TEXT("type"), TEXT("object"));
	AdditionalPropsSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> FieldsSchema = MakeShared<FMcpSchemaProperty>();
	FieldsSchema->Description = TEXT("Structured row field patch");
	FieldsSchema->RawSchema = AdditionalPropsSchema;

	TSharedPtr<FMcpSchemaProperty> BulkRowSchema = MakeShared<FMcpSchemaProperty>();
	BulkRowSchema->Type = TEXT("object");
	BulkRowSchema->Description = TEXT("Bulk-import row descriptor");
	BulkRowSchema->NestedRequired = { TEXT("row_name") };
	BulkRowSchema->Properties.Add(TEXT("row_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Row name"), true)));
	BulkRowSchema->Properties.Add(TEXT("fields"), FieldsSchema);

	FMcpSchemaProperty BulkRowsSchema;
	BulkRowsSchema.Type = TEXT("array");
	BulkRowsSchema.Description = TEXT("Rows used by bulk_import_rows");
	BulkRowsSchema.Items = BulkRowSchema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("DataTable batch operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("DataTable batch action"),
		{ TEXT("upsert_row"), TEXT("set_fields"), TEXT("delete_row"), TEXT("rename_row"), TEXT("bulk_import_rows") },
		true)));
	OperationSchema->Properties.Add(TEXT("row_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target row name"))));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New row name for rename_row"))));
	OperationSchema->Properties.Add(TEXT("fields"), FieldsSchema);
	OperationSchema->Properties.Add(TEXT("rows"), MakeShared<FMcpSchemaProperty>(BulkRowsSchema));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("DataTable batch operations");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the DataTable after edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UEditDataTableBatchTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditDataTableBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
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
	UDataTable* DataTable = FMcpAssetModifier::LoadAssetByPath<UDataTable>(AssetPath, LoadError);
	if (!DataTable)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET"), TEXT("DataTable has no row struct"));
	}

	TSet<FName> KnownRowNames;
	for (const FName& RowName : DataTable->GetRowNames())
	{
		KnownRowNames.Add(RowName);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit DataTable Batch")));
		FMcpAssetModifier::MarkModified(DataTable);
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bTableChanged = false;

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
		TArray<FString> OperationWarnings;

		if (ActionName == TEXT("upsert_row") || ActionName == TEXT("set_fields"))
		{
			FString RowNameString;
			if (!(*OperationObject)->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.IsEmpty())
			{
				OperationError = FString::Printf(TEXT("'row_name' is required for %s"), *ActionName);
			}
			else
			{
				const FName RowName(*RowNameString);
				const bool bRowExists = KnownRowNames.Contains(RowName);
				if (ActionName == TEXT("set_fields") && !bRowExists)
				{
					OperationError = FString::Printf(TEXT("Row does not exist: %s"), *RowNameString);
				}
				else
				{
					const TSharedPtr<FJsonObject> FieldsObject = GetFieldsObjectOrEmpty(*OperationObject);
					bOperationChanged = !bRowExists || FieldsObject->Values.Num() > 0;
					ResultObject->SetStringField(TEXT("row_name"), RowNameString);
					ResultObject->SetBoolField(TEXT("created"), !bRowExists);

					if (!bDryRun)
					{
						if (bRowExists)
						{
							uint8* RowData = const_cast<uint8*>(DataTable->FindRowUnchecked(RowName));
							if (!RowData)
							{
								OperationError = FString::Printf(TEXT("Failed to resolve row '%s'"), *RowNameString);
							}
							else if (!McpDataTableUtils::ApplyFieldsToRow(RowStruct, RowData, FieldsObject, OperationWarnings, OperationError))
							{
							}
						}
						else
						{
							TArray<uint8> RowBuffer;
							if (!McpDataTableUtils::CreateInitializedRowBuffer(RowStruct, RowBuffer, OperationError))
							{
							}
							else if (!McpDataTableUtils::ApplyFieldsToRow(RowStruct, RowBuffer.GetData(), FieldsObject, OperationWarnings, OperationError))
							{
								RowStruct->DestroyStruct(RowBuffer.GetData());
							}
							else
							{
								DataTable->AddRow(RowName, *reinterpret_cast<FTableRowBase*>(RowBuffer.GetData()));
								RowStruct->DestroyStruct(RowBuffer.GetData());
							}
						}
					}

					if (OperationError.IsEmpty())
					{
						KnownRowNames.Add(RowName);
						bOperationSuccess = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("delete_row"))
		{
			FString RowNameString;
			if (!(*OperationObject)->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.IsEmpty())
			{
				OperationError = TEXT("'row_name' is required for delete_row");
			}
			else
			{
				const FName RowName(*RowNameString);
				const bool bRowExists = KnownRowNames.Contains(RowName);
				ResultObject->SetStringField(TEXT("row_name"), RowNameString);
				bOperationChanged = bRowExists;

				if (!bDryRun && bRowExists)
				{
					DataTable->RemoveRow(RowName);
				}
				if (!bRowExists)
				{
					ResultObject->SetBoolField(TEXT("noop"), true);
				}

				KnownRowNames.Remove(RowName);
				bOperationSuccess = true;
			}
		}
		else if (ActionName == TEXT("rename_row"))
		{
			FString RowNameString;
			FString NewNameString;
			if (!(*OperationObject)->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.IsEmpty())
			{
				OperationError = TEXT("'row_name' is required for rename_row");
			}
			else if (!(*OperationObject)->TryGetStringField(TEXT("new_name"), NewNameString) || NewNameString.IsEmpty())
			{
				OperationError = TEXT("'new_name' is required for rename_row");
			}
			else
			{
				const FName SourceRowName(*RowNameString);
				const FName TargetRowName(*NewNameString);
				if (!KnownRowNames.Contains(SourceRowName))
				{
					OperationError = FString::Printf(TEXT("Row does not exist: %s"), *RowNameString);
				}
				else if (KnownRowNames.Contains(TargetRowName))
				{
					OperationError = FString::Printf(TEXT("Target row already exists: %s"), *NewNameString);
				}
				else
				{
					bOperationChanged = true;
					ResultObject->SetStringField(TEXT("row_name"), RowNameString);
					ResultObject->SetStringField(TEXT("new_name"), NewNameString);

					if (!bDryRun)
					{
						const uint8* SourceRow = DataTable->FindRowUnchecked(SourceRowName);
						if (!SourceRow)
						{
							OperationError = FString::Printf(TEXT("Failed to resolve row '%s'"), *RowNameString);
						}
						else
						{
							TArray<uint8> RowBuffer;
							if (!CopyRowToBuffer(RowStruct, SourceRow, RowBuffer, OperationError))
							{
							}
							else
							{
								DataTable->AddRow(TargetRowName, *reinterpret_cast<FTableRowBase*>(RowBuffer.GetData()));
								DataTable->RemoveRow(SourceRowName);
								RowStruct->DestroyStruct(RowBuffer.GetData());
							}
						}
					}

					if (OperationError.IsEmpty())
					{
						KnownRowNames.Remove(SourceRowName);
						KnownRowNames.Add(TargetRowName);
						bOperationSuccess = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("bulk_import_rows"))
		{
			const TArray<TSharedPtr<FJsonValue>>* RowsArray = nullptr;
			if (!(*OperationObject)->TryGetArrayField(TEXT("rows"), RowsArray) || !RowsArray || RowsArray->Num() == 0)
			{
				OperationError = TEXT("'rows' array is required for bulk_import_rows");
			}
			else
			{
				TArray<TSharedPtr<FJsonValue>> ImportedRows;
				for (int32 RowIndex = 0; RowIndex < RowsArray->Num(); ++RowIndex)
				{
					const TSharedPtr<FJsonObject>* RowObject = nullptr;
					if (!(*RowsArray)[RowIndex].IsValid() || !(*RowsArray)[RowIndex]->TryGetObject(RowObject) || !RowObject || !(*RowObject).IsValid())
					{
						OperationError = FString::Printf(TEXT("rows[%d] must be an object"), RowIndex);
						break;
					}

					FString RowNameString;
					if (!(*RowObject)->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.IsEmpty())
					{
						OperationError = FString::Printf(TEXT("rows[%d].row_name is required"), RowIndex);
						break;
					}

					const FName RowName(*RowNameString);
					const bool bRowExists = KnownRowNames.Contains(RowName);
					const TSharedPtr<FJsonObject> FieldsObject = GetFieldsObjectOrEmpty(*RowObject);
					bOperationChanged = true;

					if (!bDryRun)
					{
						if (bRowExists)
						{
							uint8* RowData = const_cast<uint8*>(DataTable->FindRowUnchecked(RowName));
							if (!RowData)
							{
								OperationError = FString::Printf(TEXT("Failed to resolve row '%s'"), *RowNameString);
								break;
							}
							if (!McpDataTableUtils::ApplyFieldsToRow(RowStruct, RowData, FieldsObject, OperationWarnings, OperationError))
							{
								break;
							}
						}
						else
						{
							TArray<uint8> RowBuffer;
							if (!McpDataTableUtils::CreateInitializedRowBuffer(RowStruct, RowBuffer, OperationError))
							{
								break;
							}
							if (!McpDataTableUtils::ApplyFieldsToRow(RowStruct, RowBuffer.GetData(), FieldsObject, OperationWarnings, OperationError))
							{
								RowStruct->DestroyStruct(RowBuffer.GetData());
								break;
							}
							DataTable->AddRow(RowName, *reinterpret_cast<FTableRowBase*>(RowBuffer.GetData()));
							RowStruct->DestroyStruct(RowBuffer.GetData());
						}
					}

					KnownRowNames.Add(RowName);

					TSharedPtr<FJsonObject> ImportedRowObject = MakeShareable(new FJsonObject);
					ImportedRowObject->SetStringField(TEXT("row_name"), RowNameString);
					ImportedRowObject->SetBoolField(TEXT("created"), !bRowExists);
					ImportedRows.Add(MakeShareable(new FJsonValueObject(ImportedRowObject)));
				}

				if (OperationError.IsEmpty())
				{
					ResultObject->SetArrayField(TEXT("rows"), ImportedRows);
					ResultObject->SetNumberField(TEXT("imported_count"), ImportedRows.Num());
					bOperationSuccess = true;
				}
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		AppendWarnings(OperationWarnings, WarningsArray);
		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);

		if (!bOperationSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), OperationError);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;

			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					OperationError,
					nullptr,
					BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}
		else
		{
			bTableChanged = bTableChanged || bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bTableChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(DataTable);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && bSave && bTableChanged)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(DataTable, false, SaveError))
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
