// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/CreateUserDefinedStructTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpTypeDescriptorUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace
{
	FString NormalizeIdentifier(const FString& InValue)
	{
		FString Normalized = InValue;
		Normalized.TrimStartAndEndInline();
		return Normalized.ToLower();
	}
}

FString UCreateUserDefinedStructTool::GetToolDescription() const
{
	return TEXT("Create a UserDefinedStruct asset with authored fields.");
}

TMap<FString, FMcpSchemaProperty> UCreateUserDefinedStructTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target struct asset path"), true));

	TSharedPtr<FMcpSchemaProperty> FieldSchema = MakeShared<FMcpSchemaProperty>();
	FieldSchema->Type = TEXT("object");
	FieldSchema->Description = TEXT("Struct field descriptor");
	FieldSchema->NestedRequired = { TEXT("name"), TEXT("type") };
	FieldSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Field name"), true)));
	FieldSchema->Properties.Add(TEXT("type"), McpTypeDescriptorUtils::MakeTypeDescriptorSchema(TEXT("Field type descriptor")));

	FMcpSchemaProperty FieldsSchema;
	FieldsSchema.Type = TEXT("array");
	FieldsSchema.Description = TEXT("Struct field descriptors");
	FieldsSchema.Items = FieldSchema;
	FieldsSchema.bRequired = true;
	Schema.Add(TEXT("fields"), FieldsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the struct asset after creation")));
	return Schema;
}

TArray<FString> UCreateUserDefinedStructTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("fields") };
}

FMcpToolResult UCreateUserDefinedStructTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* FieldsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("fields"), FieldsArray) || !FieldsArray || FieldsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'fields' array is required"));
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

	struct FStructFieldData
	{
		FString Name;
		FEdGraphPinType PinType;
	};

	TArray<FStructFieldData> ParsedFields;
	TSet<FString> SeenNames;
	for (int32 FieldIndex = 0; FieldIndex < FieldsArray->Num(); ++FieldIndex)
	{
		const TSharedPtr<FJsonObject>* FieldObject = nullptr;
		if (!(*FieldsArray)[FieldIndex].IsValid() || !(*FieldsArray)[FieldIndex]->TryGetObject(FieldObject) || !FieldObject || !(*FieldObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("fields[%d] must be an object"), FieldIndex));
		}

		FStructFieldData ParsedField;
		if (!(*FieldObject)->TryGetStringField(TEXT("name"), ParsedField.Name))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("fields[%d].name is required"), FieldIndex));
		}
		ParsedField.Name.TrimStartAndEndInline();
		if (ParsedField.Name.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("fields[%d].name cannot be empty"), FieldIndex));
		}

		const FString NormalizedName = NormalizeIdentifier(ParsedField.Name);
		if (SeenNames.Contains(NormalizedName))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Duplicate struct field name after normalization: '%s'"), *ParsedField.Name));
		}
		SeenNames.Add(NormalizedName);

		const TSharedPtr<FJsonObject>* TypeObject = nullptr;
		if (!(*FieldObject)->TryGetObjectField(TEXT("type"), TypeObject) || !TypeObject || !(*TypeObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("fields[%d].type is required"), FieldIndex));
		}

		FString ParseError;
		if (!McpTypeDescriptorUtils::ParseTypeDescriptor(*TypeObject, ParsedField.PinType, ParseError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("fields[%d].type: %s"), FieldIndex, *ParseError));
		}

		ParsedFields.Add(MoveTemp(ParsedField));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("dry_run"), true);
		return FMcpToolResult::StructuredJson(Result);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create User Defined Struct")));

	const FString AssetName = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create package"));
	}

	UUserDefinedStruct* UserStruct = FStructureEditorUtils::CreateUserDefinedStruct(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional);
	if (!UserStruct)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create UserDefinedStruct"));
	}

	TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(UserStruct);
	if (VarDescs.Num() == 0)
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("UserDefinedStruct did not expose any editable variable descriptors"));
	}

	for (int32 FieldIndex = 0; FieldIndex < ParsedFields.Num(); ++FieldIndex)
	{
		const FStructFieldData& Field = ParsedFields[FieldIndex];

		FString TypeError;
		if (!FStructureEditorUtils::CanHaveAMemberVariableOfType(UserStruct, Field.PinType, &TypeError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Field '%s' type is not allowed: %s"), *Field.Name, *TypeError));
		}

		FGuid TargetGuid;
		if (FieldIndex == 0)
		{
			TargetGuid = VarDescs[0].VarGuid;
			if (!FStructureEditorUtils::ChangeVariableType(UserStruct, TargetGuid, Field.PinType))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_OPERATION_FAILED"), FString::Printf(TEXT("Failed to set type for field '%s'"), *Field.Name));
			}
		}
		else
		{
			if (!FStructureEditorUtils::AddVariable(UserStruct, Field.PinType))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_OPERATION_FAILED"), FString::Printf(TEXT("Failed to add field '%s'"), *Field.Name));
			}

			TArray<FStructVariableDescription>& CurrentVarDescs = FStructureEditorUtils::GetVarDesc(UserStruct);
			TargetGuid = CurrentVarDescs.Last().VarGuid;
		}

		if (!FStructureEditorUtils::RenameVariable(UserStruct, TargetGuid, Field.Name))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_OPERATION_FAILED"), FString::Printf(TEXT("Failed to rename struct field to '%s'"), *Field.Name));
		}
	}

	FStructureEditorUtils::CompileStructure(UserStruct);
	FMcpAssetModifier::MarkPackageDirty(UserStruct);
	FAssetRegistryModule::AssetCreated(UserStruct);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(UserStruct, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("created_class"), UserStruct->GetClass()->GetName());
	Result->SetNumberField(TEXT("field_count"), ParsedFields.Num());
	return FMcpToolResult::StructuredJson(Result);
}
