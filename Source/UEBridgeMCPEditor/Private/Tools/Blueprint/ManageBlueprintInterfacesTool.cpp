#include "Tools/Blueprint/ManageBlueprintInterfacesTool.h"

#include "Tools/Blueprint/BlueprintWrapperToolUtils.h"
#include "Tools/Blueprint/EditBlueprintMembersTool.h"

FString UManageBlueprintInterfacesTool::GetToolDescription() const
{
	return TEXT("Add or remove Blueprint interfaces with optional graph synchronization. Thin wrapper around edit-blueprint-members.");
}

TMap<FString, FMcpSchemaProperty> UManageBlueprintInterfacesTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> ActionSchema = MakeShared<FMcpSchemaProperty>();
	ActionSchema->Type = TEXT("object");
	ActionSchema->NestedRequired = {TEXT("action"), TEXT("interface_path")};
	ActionSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Interface action"),
		{TEXT("add_interface"), TEXT("remove_interface")},
		true)));
	ActionSchema->Properties.Add(TEXT("interface_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Interface class path"), true)));
	ActionSchema->Properties.Add(TEXT("sync_graphs"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Conform interface graphs after edits (default true)"))));
	ActionSchema->Properties.Add(TEXT("preserve_functions"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Preserve interface graphs as normal functions when removing"))));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Interface add/remove actions");
	ActionsSchema.Items = ActionSchema;
	ActionsSchema.bRequired = true;
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Compile policy: 'never', 'if_needed', or 'always'"), {TEXT("never"), TEXT("if_needed"), TEXT("always")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after success")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Rollback the batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));
	return Schema;
}

TArray<FString> UManageBlueprintInterfacesTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("actions")};
}

FMcpToolResult UManageBlueprintInterfacesTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	TSharedPtr<FJsonObject> ForwardArguments = MakeShareable(new FJsonObject);
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("asset_path"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("compile"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("save"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("dry_run"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("rollback_on_error"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("transaction_label"));

	TArray<TSharedPtr<FJsonValue>> ForwardActions;
	ForwardActions.Reserve(ActionsArray->Num());

	for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!ActionValue.IsValid() || !ActionValue->TryGetObject(ActionObject) || !ActionObject || !(*ActionObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("Each interface action must be a valid object"));
		}

		TSharedPtr<FJsonObject> ForwardAction = MakeShareable(new FJsonObject);
		ForwardAction->Values = (*ActionObject)->Values;
		if (!ForwardAction->HasField(TEXT("sync_graphs")))
		{
			ForwardAction->SetBoolField(TEXT("sync_graphs"), true);
		}

		ForwardActions.Add(MakeShareable(new FJsonValueObject(ForwardAction)));
	}

	ForwardArguments->SetArrayField(TEXT("actions"), ForwardActions);

	UEditBlueprintMembersTool* ForwardTool = NewObject<UEditBlueprintMembersTool>();
	return ForwardTool->Execute(ForwardArguments, Context);
}
