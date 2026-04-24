// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateInputActionTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "InputAction.h"
#include "ScopedTransaction.h"

FString UCreateInputActionTool::GetToolDescription() const
{
	return TEXT("Create an Enhanced Input Action asset with common authoring defaults.");
}

TMap<FString, FMcpSchemaProperty> UCreateInputActionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input Action asset path"), true));
	Schema.Add(TEXT("value_type"), FMcpSchemaProperty::MakeEnum(
		TEXT("Input Action value type"),
		{ TEXT("bool"), TEXT("axis1d"), TEXT("axis2d"), TEXT("axis3d") }));
	Schema.Add(TEXT("description"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Localized action description")));
	Schema.Add(TEXT("consume_input"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Consume lower priority mappings")));
	Schema.Add(TEXT("consume_legacy_mappings"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Consume legacy input mappings")));
	Schema.Add(TEXT("trigger_when_paused"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Allow triggering when paused")));
	Schema.Add(TEXT("reserve_all_mappings"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Reserve all mappings")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the new asset")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UCreateInputActionTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UCreateInputActionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString ValueTypeString = GetStringArgOrDefault(Arguments, TEXT("value_type"), TEXT("bool"));
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

	EInputActionValueType ValueType = EInputActionValueType::Boolean;
	if (!GameplayToolUtils::ParseInputActionValueType(ValueTypeString, ValueType))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported value_type '%s'"), *ValueTypeString));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("value_type"), GameplayToolUtils::InputActionValueTypeToString(ValueType));

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("dry_run"), true);
		return FMcpToolResult::StructuredJson(Result);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Input Action")));

	FString CreateError;
	UInputAction* InputAction = Cast<UInputAction>(GameplayToolUtils::CreateObjectAsset(UInputAction::StaticClass(), AssetPath, CreateError));
	if (!InputAction)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), CreateError);
	}

	InputAction->Modify();
	InputAction->ValueType = ValueType;
	InputAction->ActionDescription = FText::FromString(GetStringArgOrDefault(Arguments, TEXT("description")));
	InputAction->bConsumeInput = GetBoolArgOrDefault(Arguments, TEXT("consume_input"), true);
	InputAction->bConsumesActionAndAxisMappings = GetBoolArgOrDefault(Arguments, TEXT("consume_legacy_mappings"), false);
	InputAction->bTriggerWhenPaused = GetBoolArgOrDefault(Arguments, TEXT("trigger_when_paused"), false);
	InputAction->bReserveAllMappings = GetBoolArgOrDefault(Arguments, TEXT("reserve_all_mappings"), false);
	FMcpAssetModifier::MarkPackageDirty(InputAction);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(InputAction, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, InputAction->GetClass()->GetName()));
	return FMcpToolResult::StructuredJson(Result);
}
