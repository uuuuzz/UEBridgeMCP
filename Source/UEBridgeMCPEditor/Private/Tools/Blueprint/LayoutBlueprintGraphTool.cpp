#include "Tools/Blueprint/LayoutBlueprintGraphTool.h"

#include "Tools/Blueprint/BlueprintWrapperToolUtils.h"
#include "Tools/Blueprint/EditBlueprintGraphTool.h"

FString ULayoutBlueprintGraphTool::GetToolDescription() const
{
	return TEXT("Layout an entire Blueprint graph or a node subset. Thin wrapper around edit-blueprint-graph.");
}

TMap<FString, FMcpSchemaProperty> ULayoutBlueprintGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("graph_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target graph name (defaults to EventGraph)")));
	Schema.Add(TEXT("nodes"), *BlueprintWrapperToolUtils::MakeStringArraySchema(TEXT("Optional node aliases or GUIDs to layout")));
	Schema.Add(TEXT("start_x"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Layout start X")));
	Schema.Add(TEXT("start_y"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Layout start Y")));
	Schema.Add(TEXT("spacing_x"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Horizontal layout spacing")));
	Schema.Add(TEXT("spacing_y"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Vertical layout spacing")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Compile policy: 'never', 'if_needed', or 'always'"), {TEXT("never"), TEXT("if_needed"), TEXT("always")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after success")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Rollback the batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));
	return Schema;
}

TArray<FString> ULayoutBlueprintGraphTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult ULayoutBlueprintGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	TSharedPtr<FJsonObject> ForwardArguments = MakeShareable(new FJsonObject);
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("asset_path"));
	ForwardArguments->SetBoolField(TEXT("compile"), BlueprintWrapperToolUtils::CompilePolicyToGraphBool(Arguments, false));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("save"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("dry_run"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("rollback_on_error"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, ForwardArguments, TEXT("transaction_label"));

	TSharedPtr<FJsonObject> OperationObject = MakeShareable(new FJsonObject);
	OperationObject->SetStringField(TEXT("op"), TEXT("layout_graph"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("graph_name"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("nodes"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("start_x"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("start_y"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("spacing_x"));
	BlueprintWrapperToolUtils::CopyFieldIfPresent(Arguments, OperationObject, TEXT("spacing_y"));

	TArray<TSharedPtr<FJsonValue>> Operations;
	Operations.Add(MakeShareable(new FJsonValueObject(OperationObject)));
	ForwardArguments->SetArrayField(TEXT("operations"), Operations);

	UEditBlueprintGraphTool* ForwardTool = NewObject<UEditBlueprintGraphTool>();
	return ForwardTool->Execute(ForwardArguments, Context);
}
