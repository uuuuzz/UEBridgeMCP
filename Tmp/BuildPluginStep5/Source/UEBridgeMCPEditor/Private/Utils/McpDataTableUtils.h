// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UDataTable;
class UScriptStruct;
class FProperty;

namespace McpDataTableUtils
{
	bool MatchesWildcard(const FString& Name, const FString& Pattern);

	FProperty* FindPropertyByName(const UScriptStruct* Struct, const FString& PropertyName);

	bool CreateInitializedRowBuffer(const UScriptStruct* RowStruct, TArray<uint8>& OutRowBuffer, FString& OutError);

	bool ApplyFieldsToRow(
		const UScriptStruct* RowStruct,
		uint8* RowData,
		const TSharedPtr<FJsonObject>& FieldsObject,
		TArray<FString>& OutWarnings,
		FString& OutError);

	bool BuildRowObject(
		const UScriptStruct* RowStruct,
		const FName& RowName,
		const uint8* RowData,
		TSharedPtr<FJsonObject>& OutRowObject,
		TArray<FString>& OutWarnings);

	bool SerializeDataTable(
		UDataTable* DataTable,
		const FString& RowFilter,
		bool bIncludeSchema,
		bool bIncludeRows,
		TSharedPtr<FJsonObject>& OutResult,
		TArray<FString>& OutWarnings);
}
