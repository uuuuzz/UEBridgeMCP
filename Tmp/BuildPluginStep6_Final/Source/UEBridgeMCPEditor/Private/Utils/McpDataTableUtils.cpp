// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Utils/McpDataTableUtils.h"

#include "Utils/McpPropertySerializer.h"

#include "Engine/DataTable.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	TSharedPtr<FJsonValue> SerializePropertyValueWithFallback(
		FProperty* Property,
		const uint8* RowData,
		TArray<FString>& OutWarnings)
	{
		if (!Property || !RowData)
		{
			return nullptr;
		}

		TSharedPtr<FJsonValue> JsonValue = FMcpPropertySerializer::SerializePropertyValue(
			Property,
			RowData,
			nullptr,
			0,
			4);
		if (JsonValue.IsValid())
		{
			return JsonValue;
		}

		FString ExportText;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);
		Property->ExportTextItem_Direct(ExportText, ValuePtr, nullptr, nullptr, PPF_None);
		OutWarnings.Add(FString::Printf(
			TEXT("Serialized field '%s' as string fallback because structured serialization was unavailable"),
			*Property->GetName()));
		return MakeShareable(new FJsonValueString(ExportText));
	}

	void AppendWarnings(TSharedPtr<FJsonObject>& Object, const TArray<FString>& Warnings)
	{
		if (!Object.IsValid() || Warnings.Num() == 0)
		{
			return;
		}

		TArray<TSharedPtr<FJsonValue>> WarningArray;
		for (const FString& Warning : Warnings)
		{
			WarningArray.Add(MakeShareable(new FJsonValueString(Warning)));
		}
		Object->SetArrayField(TEXT("warnings"), WarningArray);
	}
}

bool McpDataTableUtils::MatchesWildcard(const FString& Name, const FString& Pattern)
{
	return Pattern.IsEmpty() || Name.MatchesWildcard(Pattern, ESearchCase::IgnoreCase);
}

FProperty* McpDataTableUtils::FindPropertyByName(const UScriptStruct* Struct, const FString& PropertyName)
{
	if (!Struct || PropertyName.IsEmpty())
	{
		return nullptr;
	}

	if (FProperty* ExactMatch = Struct->FindPropertyByName(*PropertyName))
	{
		return ExactMatch;
	}

	for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (Property && Property->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
		{
			return Property;
		}
	}

	return nullptr;
}

bool McpDataTableUtils::CreateInitializedRowBuffer(const UScriptStruct* RowStruct, TArray<uint8>& OutRowBuffer, FString& OutError)
{
	if (!RowStruct)
	{
		OutError = TEXT("DataTable has no row struct");
		return false;
	}

	OutRowBuffer.SetNumUninitialized(RowStruct->GetStructureSize());
	RowStruct->InitializeStruct(OutRowBuffer.GetData());
	return true;
}

bool McpDataTableUtils::ApplyFieldsToRow(
	const UScriptStruct* RowStruct,
	uint8* RowData,
	const TSharedPtr<FJsonObject>& FieldsObject,
	TArray<FString>& OutWarnings,
	FString& OutError)
{
	if (!RowStruct || !RowData)
	{
		OutError = TEXT("Row struct or row data is invalid");
		return false;
	}

	if (!FieldsObject.IsValid())
	{
		return true;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : FieldsObject->Values)
	{
		FProperty* Property = FindPropertyByName(RowStruct, Pair.Key);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("Unknown row field '%s' for struct '%s'"), *Pair.Key, *RowStruct->GetPathName());
			return false;
		}

		FString DeserializeError;
		if (FMcpPropertySerializer::DeserializePropertyValue(Property, RowData, Pair.Value, DeserializeError))
		{
			continue;
		}

		FString TextValue;
		if (Pair.Value.IsValid() && Pair.Value->TryGetString(TextValue))
		{
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);
			if (Property->ImportText_Direct(*TextValue, ValuePtr, nullptr, PPF_None) != nullptr)
			{
				OutWarnings.Add(FString::Printf(
					TEXT("Applied string import fallback for row field '%s'"),
					*Property->GetName()));
				continue;
			}
		}

		OutError = FString::Printf(TEXT("Failed to deserialize row field '%s': %s"), *Pair.Key, *DeserializeError);
		return false;
	}

	return true;
}

bool McpDataTableUtils::BuildRowObject(
	const UScriptStruct* RowStruct,
	const FName& RowName,
	const uint8* RowData,
	TSharedPtr<FJsonObject>& OutRowObject,
	TArray<FString>& OutWarnings)
{
	if (!RowStruct || !RowData)
	{
		return false;
	}

	OutRowObject = MakeShareable(new FJsonObject);
	OutRowObject->SetStringField(TEXT("row_name"), RowName.ToString());

	TSharedPtr<FJsonObject> FieldsObject = MakeShareable(new FJsonObject);
	for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!Property)
		{
			continue;
		}

		TSharedPtr<FJsonValue> JsonValue = SerializePropertyValueWithFallback(Property, RowData, OutWarnings);
		if (JsonValue.IsValid())
		{
			FieldsObject->SetField(Property->GetName(), JsonValue);
		}
	}

	OutRowObject->SetObjectField(TEXT("fields"), FieldsObject);
	return true;
}

bool McpDataTableUtils::SerializeDataTable(
	UDataTable* DataTable,
	const FString& RowFilter,
	bool bIncludeSchema,
	bool bIncludeRows,
	TSharedPtr<FJsonObject>& OutResult,
	TArray<FString>& OutWarnings)
{
	if (!DataTable)
	{
		return false;
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		OutWarnings.Add(TEXT("DataTable has no row struct"));
		return false;
	}

	OutResult = MakeShareable(new FJsonObject);
	OutResult->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	OutResult->SetStringField(TEXT("row_struct_path"), RowStruct->GetPathName());

	if (bIncludeSchema)
	{
		TArray<TSharedPtr<FJsonValue>> ColumnsArray;
		for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ColumnObject = MakeShareable(new FJsonObject);
			ColumnObject->SetStringField(TEXT("name"), Property->GetName());
			ColumnObject->SetStringField(TEXT("type"), FMcpPropertySerializer::GetPropertyTypeString(Property));
			ColumnsArray.Add(MakeShareable(new FJsonValueObject(ColumnObject)));
		}
		OutResult->SetArrayField(TEXT("columns"), ColumnsArray);
	}

	const TArray<FName> AllRowNames = DataTable->GetRowNames();
	OutResult->SetNumberField(TEXT("total_row_count"), AllRowNames.Num());

	if (bIncludeRows)
	{
		TArray<TSharedPtr<FJsonValue>> RowsArray;
		for (const FName& RowName : AllRowNames)
		{
			if (!MatchesWildcard(RowName.ToString(), RowFilter))
			{
				continue;
			}

			const uint8* RowData = DataTable->FindRowUnchecked(RowName);
			if (!RowData)
			{
				OutWarnings.Add(FString::Printf(TEXT("Skipped row '%s' because its data could not be loaded"), *RowName.ToString()));
				continue;
			}

			TSharedPtr<FJsonObject> RowObject;
			if (BuildRowObject(RowStruct, RowName, RowData, RowObject, OutWarnings))
			{
				RowsArray.Add(MakeShareable(new FJsonValueObject(RowObject)));
			}
		}

		OutResult->SetArrayField(TEXT("rows"), RowsArray);
		OutResult->SetNumberField(TEXT("row_count"), RowsArray.Num());
	}
	else
	{
		OutResult->SetNumberField(TEXT("row_count"), AllRowNames.Num());
	}

	AppendWarnings(OutResult, OutWarnings);
	return true;
}
