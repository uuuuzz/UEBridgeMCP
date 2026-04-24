#include "Tools/Blueprint/CreateBlueprintEventTool.h"

#include "Tools/Blueprint/BlueprintWrapperToolUtils.h"
#include "Tools/Blueprint/EditBlueprintGraphTool.h"

FString UCreateBlueprintEventTool::GetToolDescription() const
{
	return TEXT("Create a Blueprint custom event node with inputs and metadata. Thin wrapper around edit-blueprint-graph.");
}

TMap<FString, FMcpSchemaProperty> UCreateBlueprintEventTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("graph_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target graph name (defaults to EventGraph)")));
	Schema.Add(TEXT("name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Custom event name"), true));
	Schema.Add(TEXT("inputs"), *BlueprintWrapperToolUtils::MakePinArraySchema(TEXT("Optional input pin descriptors")));
	Schema.Add(TEXT("position"), *BlueprintWrapperToolUtils::MakeNumberArraySchema(TEXT("Node position [x, y]")));
	Schema.Add(TEXT("tooltip"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional tooltip")));
	Schema.Add(TEXT("call_in_editor"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the event is callable in editor")));
	Schema.Add(TEXT("metadata"), *BlueprintWrapperToolUtils::MakeOpenObjectSchema(TEXT("Optional event metadata map")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Compile policy: 'never', 'if_needed', or 'always'"), {TEXT("never"), TEXT("if_needed"), TEXT("always")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after success")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Rollback the batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));
	return Schema;
}

TArray<FString> UCreateBlueprintEventTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("name")};
}

FMcpToolResult UCreateBlueprintEventTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	TSharedPtr<FJsonObject> ForwardArguments = MakeShareable(new FJsonObject);
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("asset_path"));
	ForwardArguments->SetBoolField(TEXT("compile"), BlueprintWrapperToolUtils::CompilePolicyToGraphBool(Arguments, true));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("save"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("dry_run"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("rollback_on_error"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("transaction_label"));

	TSharedPtr<FJsonObject> OperationObject = MakeShareable(new FJsonObject);
	OperationObject->SetStringField(TEXT("op"), TEXT("create_custom_event"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("graph_name"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("name"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("inputs"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("position"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("tooltip"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("call_in_editor"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("metadata"));

	TArray<TSharedPtr<FJsonValue>> Operations;
	Operations.Add(MakeShareable(new FJsonValueObject(OperationObject)));
	ForwardArguments->SetArrayField(TEXT("operations"), Operations);

	UEditBlueprintGraphTool* ForwardTool = NewObject<UEditBlueprintGraphTool>();
	return ForwardTool->Execute(ForwardArguments, Context);
}
