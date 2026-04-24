// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/CreateUserDefinedEnumTool.h"

#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
	FString NormalizeIdentifier(const FString& InValue)
	{
		FString Normalized = InValue;
		Normalized.TrimStartAndEndInline();
		return Normalized.ToLower();
	}
}

FString UCreateUserDefinedEnumTool::GetToolDescription() const
{
	return TEXT("Create a UserDefinedEnum asset with authored entries.");
}

TMap<FString, FMcpSchemaProperty> UCreateUserDefinedEnumTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target enum asset path"), true));

	TSharedPtr<FMcpSchemaProperty> EntrySchema = MakeShared<FMcpSchemaProperty>();
	EntrySchema->Type = TEXT("object");
	EntrySchema->Description = TEXT("Enum entry descriptor");
	EntrySchema->NestedRequired = { TEXT("name") };
	EntrySchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Enumerator name"), true)));
	EntrySchema->Properties.Add(TEXT("display_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional display name"))));

	FMcpSchemaProperty EntriesSchema;
	EntriesSchema.Type = TEXT("array");
	EntriesSchema.Description = TEXT("Enumerator entries");
	EntriesSchema.Items = EntrySchema;
	EntriesSchema.bRequired = true;
	Schema.Add(TEXT("entries"), EntriesSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the enum asset after creation")));
	return Schema;
}

TArray<FString> UCreateUserDefinedEnumTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("entries") };
}

FMcpToolResult UCreateUserDefinedEnumTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* EntriesArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("entries"), EntriesArray) || !EntriesArray || EntriesArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'entries' array is required"));
	}

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	struct FEnumEntryData
	{
		FString Name;
		FString DisplayName;
	};

	TArray<FEnumEntryData> ParsedEntries;
	TSet<FString> SeenNames;
	for (int32 EntryIndex = 0; EntryIndex < EntriesArray->Num(); ++EntryIndex)
	{
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!(*EntriesArray)[EntryIndex].IsValid() || !(*EntriesArray)[EntryIndex]->TryGetObject(EntryObject) || !EntryObject || !(*EntryObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("entries[%d] must be an object"), EntryIndex));
		}

		FEnumEntryData ParsedEntry;
		if (!(*EntryObject)->TryGetStringField(TEXT("name"), ParsedEntry.Name))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("entries[%d].name is required"), EntryIndex));
		}
		ParsedEntry.Name.TrimStartAndEndInline();
		(*EntryObject)->TryGetStringField(TEXT("display_name"), ParsedEntry.DisplayName);

		if (ParsedEntry.Name.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("entries[%d].name cannot be empty"), EntryIndex));
		}

		const FString NormalizedName = NormalizeIdentifier(ParsedEntry.Name);
		if (SeenNames.Contains(NormalizedName))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Duplicate enum entry name after normalization: '%s'"), *ParsedEntry.Name));
		}
		SeenNames.Add(NormalizedName);
		ParsedEntries.Add(MoveTemp(ParsedEntry));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	TArray<TSharedPtr<FJsonValue>> EntryResults;
	for (const FEnumEntryData& Entry : ParsedEntries)
	{
		TSharedPtr<FJsonObject> EntryObject = MakeShareable(new FJsonObject);
		EntryObject->SetStringField(TEXT("name"), Entry.Name);
		if (!Entry.DisplayName.IsEmpty())
		{
			EntryObject->SetStringField(TEXT("display_name"), Entry.DisplayName);
		}
		EntryResults.Add(MakeShareable(new FJsonValueObject(EntryObject)));
	}
	Result->SetArrayField(TEXT("entries"), EntryResults);

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("dry_run"), true);
		return FMcpToolResult::StructuredJson(Result);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create User Defined Enum")));

	const FString AssetName = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create package"));
	}

	UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional));
	if (!UserEnum)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create UserDefinedEnum"));
	}

	for (const FEnumEntryData& Entry : ParsedEntries)
	{
		if (!FEnumEditorUtils::IsProperNameForUserDefinedEnumerator(UserEnum, Entry.Name))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Invalid enum entry name: '%s'"), *Entry.Name));
		}
	}

	TArray<TPair<FName, int64>> EnumNames;
	for (int32 Index = 0; Index < ParsedEntries.Num(); ++Index)
	{
		EnumNames.Emplace(*UserEnum->GenerateFullEnumName(*ParsedEntries[Index].Name), Index);
	}
	UserEnum->SetEnums(EnumNames, UEnum::ECppForm::Namespaced);
	FEnumEditorUtils::EnsureAllDisplayNamesExist(UserEnum);

	for (int32 Index = 0; Index < ParsedEntries.Num(); ++Index)
	{
		if (!ParsedEntries[Index].DisplayName.IsEmpty())
		{
			FEnumEditorUtils::SetEnumeratorDisplayName(UserEnum, Index, FText::FromString(ParsedEntries[Index].DisplayName));
		}
	}

	FMcpAssetModifier::MarkPackageDirty(UserEnum);
	FAssetRegistryModule::AssetCreated(UserEnum);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(UserEnum, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("created_class"), UserEnum->GetClass()->GetName());
	return FMcpToolResult::StructuredJson(Result);
}
