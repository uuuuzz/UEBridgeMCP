// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/MetaSound/CreateMetaSoundSourceTool.h"

#include "Tools/MetaSound/MetaSoundToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundSource.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FMcpSchemaProperty> MakeInputDefinitionSchema()
	{
		TSharedPtr<FMcpSchemaProperty> InputSchema = MakeShared<FMcpSchemaProperty>();
		InputSchema->Type = TEXT("object");
		InputSchema->Description = TEXT("MetaSound graph input definition");
		InputSchema->NestedRequired = { TEXT("name"), TEXT("type") };
		InputSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input name"), true)));
		InputSchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
			TEXT("Input data type"),
			{ TEXT("bool"), TEXT("int32"), TEXT("float"), TEXT("string"), TEXT("trigger") },
			true)));
		InputSchema->Properties.Add(TEXT("default"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Input default value"))));
		InputSchema->Properties.Add(TEXT("constructor_input"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create as constructor input"))));
		return InputSchema;
	}
}

FString UCreateMetaSoundSourceTool::GetToolDescription() const
{
	return TEXT("Create a MetaSound Source asset through the official MetaSound Source Builder, with optional v1 graph inputs and output format.");
}

TMap<FString, FMcpSchemaProperty> UCreateMetaSoundSourceTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination MetaSound Source asset path"), true));
	Schema.Add(TEXT("output_format"), FMcpSchemaProperty::MakeEnum(TEXT("Output audio format"), { TEXT("mono"), TEXT("stereo"), TEXT("quad"), TEXT("5.1"), TEXT("7.1") }));
	Schema.Add(TEXT("one_shot"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create a one-shot source graph")));
	Schema.Add(TEXT("author"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Author metadata written by the MetaSound editor subsystem")));

	FMcpSchemaProperty InputsSchema;
	InputsSchema.Type = TEXT("array");
	InputsSchema.Description = TEXT("Optional graph inputs to create after source builder initialization");
	InputsSchema.Items = MakeInputDefinitionSchema();
	Schema.Add(TEXT("inputs"), InputsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the created MetaSound Source")));
	return Schema;
}

FMcpToolResult UCreateMetaSoundSourceTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString OutputFormatName = GetStringArgOrDefault(Arguments, TEXT("output_format"), TEXT("mono"));
	const FString Author = GetStringArgOrDefault(Arguments, TEXT("author"), TEXT("UEBridgeMCP"));
	const bool bOneShot = GetBoolArgOrDefault(Arguments, TEXT("one_shot"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Destination MetaSound Source already exists"), Details);
	}

	EMetaSoundOutputAudioFormat OutputFormat = EMetaSoundOutputAudioFormat::Mono;
	FString FormatError;
	if (!MetaSoundToolUtils::TryResolveOutputFormat(OutputFormatName, OutputFormat, FormatError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FormatError);
	}

	const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
	Arguments->TryGetArrayField(TEXT("inputs"), Inputs);

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create MetaSound Source")));

	EMetaSoundBuilderResult BuilderResult = EMetaSoundBuilderResult::Failed;
	FMetaSoundBuilderNodeOutputHandle OnPlayNodeOutput;
	FMetaSoundBuilderNodeInputHandle OnFinishedNodeInput;
	TArray<FMetaSoundBuilderNodeInputHandle> AudioOutNodeInputs;
	UMetaSoundSourceBuilder* Builder = UMetaSoundBuilderSubsystem::GetChecked().CreateSourceBuilder(
		FName(*AssetName),
		OnPlayNodeOutput,
		OnFinishedNodeInput,
		AudioOutNodeInputs,
		BuilderResult,
		OutputFormat,
		bOneShot);

	if (!Builder || BuilderResult != EMetaSoundBuilderResult::Succeeded)
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_METASOUND_CREATE_FAILED"),
			FString::Printf(TEXT("Failed to create MetaSound Source builder: %s"), *MetaSoundToolUtils::BuilderResultToString(BuilderResult)));
	}

	TArray<TSharedPtr<FJsonValue>> OperationResults;
	if (Inputs)
	{
		for (int32 Index = 0; Index < Inputs->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* InputObject = nullptr;
			if (!(*Inputs)[Index].IsValid() || !(*Inputs)[Index]->TryGetObject(InputObject) || !InputObject || !(*InputObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("inputs[%d] must be an object"), Index));
			}

			FString Name;
			FString TypeName;
			(*InputObject)->TryGetStringField(TEXT("name"), Name);
			(*InputObject)->TryGetStringField(TEXT("type"), TypeName);
			if (Name.IsEmpty() || TypeName.IsEmpty())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("inputs[%d] requires 'name' and 'type'"), Index));
			}

			FName DataType;
			FString TypeError;
			if (!MetaSoundToolUtils::TryResolveDataType(TypeName, DataType, TypeError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TypeError);
			}

			FMetasoundFrontendLiteral DefaultLiteral;
			FString LiteralError;
			if (!MetaSoundToolUtils::TryReadLiteral(TypeName, (*InputObject)->TryGetField(TEXT("default")), DefaultLiteral, LiteralError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), LiteralError);
			}

			EMetaSoundBuilderResult AddResult = EMetaSoundBuilderResult::Failed;
			bool bConstructorInput = false;
			(*InputObject)->TryGetBoolField(TEXT("constructor_input"), bConstructorInput);
			FMetaSoundBuilderNodeOutputHandle Handle = Builder->AddGraphInputNode(FName(*Name), DataType, DefaultLiteral, AddResult, bConstructorInput);

			TSharedPtr<FJsonObject> ItemResult = MakeShareable(new FJsonObject);
			ItemResult->SetNumberField(TEXT("index"), Index);
			ItemResult->SetStringField(TEXT("action"), TEXT("add_graph_input"));
			ItemResult->SetStringField(TEXT("name"), Name);
			ItemResult->SetStringField(TEXT("type"), DataType.ToString());
			ItemResult->SetBoolField(TEXT("success"), AddResult == EMetaSoundBuilderResult::Succeeded);
			ItemResult->SetObjectField(TEXT("output_handle"), MetaSoundToolUtils::SerializeOutputHandle(Handle));
			ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(AddResult));
			OperationResults.Add(MakeShareable(new FJsonValueObject(ItemResult)));

			if (AddResult != EMetaSoundBuilderResult::Succeeded)
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_METASOUND_GRAPH_EDIT_FAILED"), FString::Printf(TEXT("Failed to add graph input '%s'"), *Name));
			}
		}
	}

	Builder->InitNodeLocations();
	EMetaSoundBuilderResult BuildResult = EMetaSoundBuilderResult::Failed;
	TScriptInterface<IMetaSoundDocumentInterface> BuiltInterface = UMetaSoundEditorSubsystem::GetChecked().BuildToAsset(
		Builder,
		Author,
		AssetName,
		PackagePath,
		BuildResult);

	UMetaSoundSource* CreatedSource = Cast<UMetaSoundSource>(BuiltInterface.GetObject());
	if (!CreatedSource || BuildResult != EMetaSoundBuilderResult::Succeeded)
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_METASOUND_CREATE_FAILED"),
			FString::Printf(TEXT("Failed to build MetaSound Source asset: %s"), *MetaSoundToolUtils::BuilderResultToString(BuildResult)));
	}

	FMcpAssetModifier::MarkPackageDirty(CreatedSource);
	FAssetRegistryModule::AssetCreated(CreatedSource);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (bSave)
	{
		MetaSoundToolUtils::SaveAsset(CreatedSource, Warnings);
	}

	TSharedPtr<FJsonObject> Result = MetaSoundToolUtils::SerializeSourceSummary(CreatedSource, false);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(BuildResult));
	Result->SetStringField(TEXT("creation_mode"), TEXT("source_builder"));
	Result->SetObjectField(TEXT("on_play_output_handle"), MetaSoundToolUtils::SerializeOutputHandle(OnPlayNodeOutput));
	Result->SetObjectField(TEXT("on_finished_input_handle"), MetaSoundToolUtils::SerializeInputHandle(OnFinishedNodeInput));
	Result->SetNumberField(TEXT("audio_output_count"), AudioOutNodeInputs.Num());
	Result->SetArrayField(TEXT("operations"), OperationResults);
	Result->SetBoolField(TEXT("saved"), bSave && Warnings.Num() == 0);
	Result->SetBoolField(TEXT("needs_save"), CreatedSource->GetOutermost()->IsDirty());
	Result->SetArrayField(TEXT("warnings"), Warnings);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("MetaSound Source created"));
}
