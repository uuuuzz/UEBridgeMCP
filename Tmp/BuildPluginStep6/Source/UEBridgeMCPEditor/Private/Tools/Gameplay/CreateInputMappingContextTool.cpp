// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateInputMappingContextTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "ScopedTransaction.h"

FString UCreateInputMappingContextTool::GetToolDescription() const
{
	return TEXT("Create an Enhanced Input Mapping Context asset with optional initial key mappings.");
}

TMap<FString, FMcpSchemaProperty> UCreateInputMappingContextTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input Mapping Context asset path"), true));
	Schema.Add(TEXT("description"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Localized context description")));

	TSharedPtr<FMcpSchemaProperty> MappingSchema = MakeShared<FMcpSchemaProperty>();
	MappingSchema->Type = TEXT("object");
	MappingSchema->Description = TEXT("Initial key mapping");
	MappingSchema->NestedRequired = { TEXT("action_asset_path"), TEXT("key") };
	MappingSchema->Properties.Add(TEXT("action_asset_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input Action asset path"), true)));
	MappingSchema->Properties.Add(TEXT("key"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input key"), true)));

	FMcpSchemaProperty MappingsSchema;
	MappingsSchema.Type = TEXT("array");
	MappingsSchema.Description = TEXT("Optional initial input mappings");
	MappingsSchema.Items = MappingSchema;
	Schema.Add(TEXT("initial_mappings"), MappingsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the new asset")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UCreateInputMappingContextTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UCreateInputMappingContextTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	const TArray<TSharedPtr<FJsonValue>>* InitialMappings = nullptr;
	Arguments->TryGetArrayField(TEXT("initial_mappings"), InitialMappings);

	TArray<TPair<FString, FString>> ValidatedMappings;
	if (InitialMappings)
	{
		for (int32 MappingIndex = 0; MappingIndex < InitialMappings->Num(); ++MappingIndex)
		{
			const TSharedPtr<FJsonObject>* MappingObject = nullptr;
			if (!(*InitialMappings)[MappingIndex].IsValid() || !(*InitialMappings)[MappingIndex]->TryGetObject(MappingObject) || !MappingObject || !(*MappingObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("initial_mappings[%d] must be an object"), MappingIndex));
			}

			FString ActionAssetPath;
			FString KeyName;
			if (!(*MappingObject)->TryGetStringField(TEXT("action_asset_path"), ActionAssetPath) || !(*MappingObject)->TryGetStringField(TEXT("key"), KeyName))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("initial_mappings[%d] requires 'action_asset_path' and 'key'"), MappingIndex));
			}

			FString KeyError;
			FKey ParsedKey;
			if (!GameplayToolUtils::ParseKey(KeyName, ParsedKey, KeyError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("initial_mappings[%d].key: %s"), MappingIndex, *KeyError));
			}

			FString LoadError;
			if (!FMcpAssetModifier::LoadAssetByPath<UInputAction>(ActionAssetPath, LoadError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), FString::Printf(TEXT("initial_mappings[%d].action_asset_path: %s"), MappingIndex, *LoadError));
			}

			ValidatedMappings.Emplace(ActionAssetPath, KeyName);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetNumberField(TEXT("initial_mapping_count"), ValidatedMappings.Num());
		return FMcpToolResult::StructuredJson(Result);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Input Mapping Context")));

	FString CreateError;
	UInputMappingContext* MappingContext = Cast<UInputMappingContext>(GameplayToolUtils::CreateObjectAsset(UInputMappingContext::StaticClass(), AssetPath, CreateError));
	if (!MappingContext)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), CreateError);
	}

	MappingContext->Modify();
	MappingContext->ContextDescription = FText::FromString(GetStringArgOrDefault(Arguments, TEXT("description")));

	TArray<TSharedPtr<FJsonValue>> MappingArray;
	for (const TPair<FString, FString>& Entry : ValidatedMappings)
	{
		FString LoadError;
		UInputAction* InputAction = FMcpAssetModifier::LoadAssetByPath<UInputAction>(Entry.Key, LoadError);
		if (!InputAction)
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}

		FString KeyError;
		FKey ParsedKey;
		if (!GameplayToolUtils::ParseKey(Entry.Value, ParsedKey, KeyError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), KeyError);
		}

		FEnhancedActionKeyMapping& NewMapping = MappingContext->MapKey(InputAction, ParsedKey);
		MappingArray.Add(MakeShareable(new FJsonValueObject(GameplayToolUtils::SerializeInputMapping(NewMapping))));
	}

	FMcpAssetModifier::MarkPackageDirty(MappingContext);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(MappingContext, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("initial_mapping_count"), MappingArray.Num());
	Result->SetArrayField(TEXT("mappings"), MappingArray);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, MappingContext->GetClass()->GetName()));
	return FMcpToolResult::StructuredJson(Result);
}
