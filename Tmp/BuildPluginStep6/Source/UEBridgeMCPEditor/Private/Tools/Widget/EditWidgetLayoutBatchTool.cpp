// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Widget/EditWidgetLayoutBatchTool.h"
#include "Tools/Widget/EditWidgetBlueprintTool.h"

FString UEditWidgetLayoutBatchTool::GetToolDescription() const
{
	return TEXT("Batch edit Widget Blueprint layout slot data. "
		"This is a thin layout-focused wrapper around edit-widget-blueprint.");
}

TMap<FString, FMcpSchemaProperty> UEditWidgetLayoutBatchTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Widget Blueprint asset path"),
		true));

	TSharedPtr<FJsonObject> GenericObjectRawSchema = MakeShareable(new FJsonObject);
	GenericObjectRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	GenericObjectRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> SlotSchema = MakeShared<FMcpSchemaProperty>();
	SlotSchema->Description = TEXT("Slot layout descriptor. Supports anchors, offsets, position, size, alignment, auto_size, z_order, and raw slot property paths.");
	SlotSchema->RawSchema = GenericObjectRawSchema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Layout edit operation");
	OperationSchema->NestedRequired = { TEXT("widget_name"), TEXT("slot") };
	OperationSchema->Properties.Add(TEXT("widget_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Target widget name"),
		true)));
	OperationSchema->Properties.Add(TEXT("slot"), SlotSchema);
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Optional explicit action. Defaults to set_slot_properties."),
		{ TEXT("set_slot_properties") })));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Array of layout operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy: 'never', 'if_needed', or 'always'"),
		{ TEXT("never"), TEXT("if_needed"), TEXT("always") }));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after successful edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));

	return Schema;
}

TArray<FString> UEditWidgetLayoutBatchTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditWidgetLayoutBatchTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	TSharedPtr<FJsonObject> ForwardArguments = MakeShareable(new FJsonObject());
	ForwardArguments->Values = Arguments->Values;

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), OperationsArray) || !OperationsArray)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	TArray<TSharedPtr<FJsonValue>> ForwardOperations;
	ForwardOperations.Reserve(OperationsArray->Num());

	for (const TSharedPtr<FJsonValue>& OperationValue : *OperationsArray)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!OperationValue.IsValid() || !OperationValue->TryGetObject(OperationObject) || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("Each layout operation must be a valid object"));
		}

		TSharedPtr<FJsonObject> ForwardOperation = MakeShareable(new FJsonObject());
		ForwardOperation->Values = (*OperationObject)->Values;
		if (!ForwardOperation->HasField(TEXT("action")))
		{
			ForwardOperation->SetStringField(TEXT("action"), TEXT("set_slot_properties"));
		}

		ForwardOperations.Add(MakeShareable(new FJsonValueObject(ForwardOperation)));
	}

	ForwardArguments->SetArrayField(TEXT("operations"), ForwardOperations);

	UEditWidgetBlueprintTool* ForwardTool = NewObject<UEditWidgetBlueprintTool>();
	return ForwardTool->Execute(ForwardArguments, Context);
}
