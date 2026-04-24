#include "Tools/Blueprint/EditBlueprintFunctionSignatureTool.h"

#include "Tools/Blueprint/BlueprintWrapperToolUtils.h"
#include "Tools/Blueprint/EditBlueprintMembersTool.h"

FString UEditBlueprintFunctionSignatureTool::GetToolDescription() const
{
	return TEXT("Edit the signature and metadata of an existing Blueprint function. Thin wrapper around edit-blueprint-members.");
}

TMap<FString, FMcpSchemaProperty> UEditBlueprintFunctionSignatureTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing function name"), true));
	Schema.Add(TEXT("inputs"), *BlueprintWrapperToolUtils::MakePinArraySchema(TEXT("Optional input pin descriptors")));
	Schema.Add(TEXT("outputs"), *BlueprintWrapperToolUtils::MakePinArraySchema(TEXT("Optional output pin descriptors")));
	Schema.Add(TEXT("category"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Function category")));
	Schema.Add(TEXT("tooltip"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Function tooltip")));
	Schema.Add(TEXT("pure"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the function is pure")));
	Schema.Add(TEXT("const"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the function is const")));
	Schema.Add(TEXT("call_in_editor"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the function is callable in editor")));
	Schema.Add(TEXT("metadata"), *BlueprintWrapperToolUtils::MakeOpenObjectSchema(TEXT("Optional function metadata map")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Compile policy: 'never', 'if_needed', or 'always'"), {TEXT("never"), TEXT("if_needed"), TEXT("always")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after success")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Rollback the batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));
	return Schema;
}

TArray<FString> UEditBlueprintFunctionSignatureTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("name")};
}

FMcpToolResult UEditBlueprintFunctionSignatureTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	TSharedPtr<FJsonObject> ForwardArguments = MakeShareable(new FJsonObject);
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("asset_path"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("compile"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("save"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("dry_run"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("rollback_on_error"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("transaction_label"));

	TSharedPtr<FJsonObject> ActionObject = MakeShareable(new FJsonObject);
	ActionObject->SetStringField(TEXT("action"), TEXT("set_function_signature"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("name"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("inputs"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("outputs"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("category"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("tooltip"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("pure"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("const"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("call_in_editor"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ActionObject, TEXT("metadata"));

	TArray<TSharedPtr<FJsonValue>> Actions;
	Actions.Add(MakeShareable(new FJsonValueObject(ActionObject)));
	ForwardArguments->SetArrayField(TEXT("actions"), Actions);

	UEditBlueprintMembersTool* ForwardTool = NewObject<UEditBlueprintMembersTool>();
	return ForwardTool->Execute(ForwardArguments, Context);
}
