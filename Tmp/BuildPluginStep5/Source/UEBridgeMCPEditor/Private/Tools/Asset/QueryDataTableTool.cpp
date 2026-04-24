// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/QueryDataTableTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpDataTableUtils.h"

#include "Engine/DataTable.h"

FString UQueryDataTableTool::GetToolDescription() const
{
	return TEXT("Query a DataTable with structured schema and row fields.");
}

TMap<FString, FMcpSchemaProperty> UQueryDataTableTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("DataTable asset path"), true));
	Schema.Add(TEXT("row_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional wildcard row-name filter")));
	Schema.Add(TEXT("include_schema"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include reflected row schema columns")));
	Schema.Add(TEXT("include_rows"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include structured row data")));
	return Schema;
}

TArray<FString> UQueryDataTableTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UQueryDataTableTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString RowFilter = GetStringArgOrDefault(Arguments, TEXT("row_filter"));
	const bool bIncludeSchema = GetBoolArgOrDefault(Arguments, TEXT("include_schema"), true);
	const bool bIncludeRows = GetBoolArgOrDefault(Arguments, TEXT("include_rows"), true);

	FString LoadError;
	UDataTable* DataTable = FMcpAssetModifier::LoadAssetByPath<UDataTable>(AssetPath, LoadError);
	if (!DataTable)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TArray<FString> Warnings;
	TSharedPtr<FJsonObject> Result;
	if (!McpDataTableUtils::SerializeDataTable(DataTable, RowFilter, bIncludeSchema, bIncludeRows, Result, Warnings) || !Result.IsValid())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_DATATABLE_SERIALIZE_FAILED"), TEXT("Failed to serialize DataTable"));
	}

	Result->SetStringField(TEXT("tool"), GetToolName());
	return FMcpToolResult::StructuredJson(Result);
}
