// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Write/AddDataTableRowTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpDataTableUtils.h"
#include "UEBridgeMCPEditor.h"
#include "Engine/DataTable.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

FString UAddDataTableRowTool::GetToolDescription() const
{
	return TEXT("Add a new row to a DataTable.");
}

TMap<FString, FMcpSchemaProperty> UAddDataTableRowTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("DataTable asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FMcpSchemaProperty RowName;
	RowName.Type = TEXT("string");
	RowName.Description = TEXT("Name for the new row");
	RowName.bRequired = true;
	Schema.Add(TEXT("row_name"), RowName);

	FMcpSchemaProperty RowData;
	RowData.Type = TEXT("string");
	RowData.Description = TEXT("Row data as JSON string with property names matching the row struct. Example: {\"Name\":\"Value\",\"Count\":5}");
	RowData.bRequired = false;
	Schema.Add(TEXT("row_data"), RowData);

	TSharedPtr<FJsonObject> FieldsRawSchema = MakeShareable(new FJsonObject);
	FieldsRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	FieldsRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	FMcpSchemaProperty FieldsSchema;
	FieldsSchema.Description = TEXT("Structured row fields object. Preferred over row_data for new callers.");
	FieldsSchema.RawSchema = FieldsRawSchema;
	Schema.Add(TEXT("fields"), FieldsSchema);

	return Schema;
}

TArray<FString> UAddDataTableRowTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("row_name") };
}

FMcpToolResult UAddDataTableRowTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString RowName = GetStringArgOrDefault(Arguments, TEXT("row_name"));
	FString RowDataString = GetStringArgOrDefault(Arguments, TEXT("row_data"));

	// Parse row_data JSON string if provided, but prefer the structured fields object.
	TSharedPtr<FJsonObject> RowFields = nullptr;
	const TSharedPtr<FJsonObject>* FieldsObject = nullptr;
	if (Arguments->TryGetObjectField(TEXT("fields"), FieldsObject) && FieldsObject && (*FieldsObject).IsValid())
	{
		RowFields = *FieldsObject;
	}
	else if (!RowDataString.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RowDataString);
		if (!FJsonSerializer::Deserialize(Reader, RowFields) || !RowFields.IsValid())
		{
			return FMcpToolResult::Error(TEXT("row_data must be valid JSON string"));
		}
	}

	if (AssetPath.IsEmpty() || RowName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("asset_path and row_name are required"));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("add-datatable-row: %s to %s"), *RowName, *AssetPath);

	// Load the DataTable
	FString LoadError;
	UDataTable* DataTable = FMcpAssetModifier::LoadAssetByPath<UDataTable>(AssetPath, LoadError);
	if (!DataTable)
	{
		return FMcpToolResult::Error(LoadError);
	}

	// Check if row already exists
	if (DataTable->FindRowUnchecked(FName(*RowName)))
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Row already exists: %s"), *RowName));
	}

	// Get the row struct
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		return FMcpToolResult::Error(TEXT("DataTable has no row struct"));
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "AddRow", "Add row {0} to {1}"),
			FText::FromString(RowName), FText::FromString(AssetPath)));

	FMcpAssetModifier::MarkModified(DataTable);

	// Create a new row with default values
	TArray<uint8> RowBuffer;
	if (!McpDataTableUtils::CreateInitializedRowBuffer(RowStruct, RowBuffer, LoadError))
	{
		return FMcpToolResult::Error(LoadError);
	}

	// If row data is provided, populate the reflected fields.
	TArray<FString> Warnings;
	if (RowFields.IsValid())
	{
		FString ApplyError;
		if (!McpDataTableUtils::ApplyFieldsToRow(RowStruct, RowBuffer.GetData(), RowFields, Warnings, ApplyError))
		{
			RowStruct->DestroyStruct(RowBuffer.GetData());
			return FMcpToolResult::Error(ApplyError);
		}
	}

	// Add the row
	DataTable->AddRow(FName(*RowName), *reinterpret_cast<FTableRowBase*>(RowBuffer.GetData()));
	RowStruct->DestroyStruct(RowBuffer.GetData());

	FMcpAssetModifier::MarkPackageDirty(DataTable);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("row_name"), RowName);
	Result->SetBoolField(TEXT("needs_save"), true);
	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarningArray;
		for (const FString& Warning : Warnings)
		{
			WarningArray.Add(MakeShareable(new FJsonValueString(Warning)));
		}
		Result->SetArrayField(TEXT("warnings"), WarningArray);
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("add-datatable-row: Added row %s"), *RowName);

	return FMcpToolResult::Json(Result);
}
