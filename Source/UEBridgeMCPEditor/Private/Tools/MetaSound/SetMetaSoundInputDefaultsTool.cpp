// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/MetaSound/SetMetaSoundInputDefaultsTool.h"

#include "Tools/MetaSound/MetaSoundToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundSource.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FMcpSchemaProperty> MakeDefaultSchema()
	{
		TSharedPtr<FMcpSchemaProperty> DefaultSchema = MakeShared<FMcpSchemaProperty>();
		DefaultSchema->Type = TEXT("object");
		DefaultSchema->Description = TEXT("MetaSound input default edit");
		DefaultSchema->NestedRequired = { TEXT("name"), TEXT("type"), TEXT("value") };
		DefaultSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Graph input name"), true)));
		DefaultSchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
			TEXT("Input data type"),
			{ TEXT("bool"), TEXT("int32"), TEXT("float"), TEXT("string") },
			true)));
		DefaultSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Default value"), true)));
		return DefaultSchema;
	}
}

FString USetMetaSoundInputDefaultsTool::GetToolDescription() const
{
	return TEXT("Batch set default values for existing MetaSound Source graph inputs. V1 supports bool/int32/float/string defaults.");
}

TMap<FString, FMcpSchemaProperty> USetMetaSoundInputDefaultsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("MetaSound Source asset path"), true));

	FMcpSchemaProperty DefaultsSchema;
	DefaultsSchema.Type = TEXT("array");
	DefaultsSchema.Description = TEXT("Input default edits");
	DefaultsSchema.bRequired = true;
	DefaultsSchema.Items = MakeDefaultSchema();
	Schema.Add(TEXT("defaults"), DefaultsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only without mutating the asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on first failed edit")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the asset after successful edits")));
	return Schema;
}

FMcpToolResult USetMetaSoundInputDefaultsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Defaults = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("defaults"), Defaults) || !Defaults || Defaults->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'defaults' array is required"));
	}

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	if (!MetaSoundToolUtils::TryLoadSource(AssetPath, Source, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UMetaSoundBuilderBase* Builder = nullptr;
	FString BuildError;
	if (!MetaSoundToolUtils::TryBeginBuilding(Source, Builder, BuildError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_METASOUND_BUILDER_FAILED"), BuildError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Set MetaSound Input Defaults")));
		Source->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	bool bAnyFailed = false;
	bool bAnyChanged = false;
	int32 Succeeded = 0;
	int32 Failed = 0;

	for (int32 Index = 0; Index < Defaults->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* DefaultObject = nullptr;
		if (!(*Defaults)[Index].IsValid() || !(*Defaults)[Index]->TryGetObject(DefaultObject) || !DefaultObject || !(*DefaultObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("defaults[%d] must be an object"), Index));
		}

		FString Name;
		FString TypeName;
		(*DefaultObject)->TryGetStringField(TEXT("name"), Name);
		(*DefaultObject)->TryGetStringField(TEXT("type"), TypeName);
		const TSharedPtr<FJsonValue> Value = (*DefaultObject)->TryGetField(TEXT("value"));

		TSharedPtr<FJsonObject> ItemResult = MakeShareable(new FJsonObject);
		ItemResult->SetNumberField(TEXT("index"), Index);
		ItemResult->SetStringField(TEXT("name"), Name);
		ItemResult->SetStringField(TEXT("type"), TypeName);

		bool bSuccess = false;
		FString Error;
		if (Name.IsEmpty() || TypeName.IsEmpty() || !Value.IsValid())
		{
			Error = TEXT("'name', 'type', and 'value' are required");
		}
		else
		{
			FMetasoundFrontendLiteral Literal;
			if (!MetaSoundToolUtils::TryReadLiteral(TypeName, Value, Literal, Error))
			{
				// Error already populated.
			}
			else if (!bDryRun)
			{
				EMetaSoundBuilderResult SetResult = EMetaSoundBuilderResult::Failed;
				Builder->SetGraphInputDefault(FName(*Name), Literal, SetResult);
				bSuccess = SetResult == EMetaSoundBuilderResult::Succeeded;
				if (!bSuccess)
				{
					Error = FString::Printf(TEXT("Failed to set MetaSound input default '%s': %s"), *Name, *MetaSoundToolUtils::BuilderResultToString(SetResult));
				}
			}
			else
			{
				EMetaSoundBuilderResult GetResult = EMetaSoundBuilderResult::Failed;
				Builder->GetGraphInputDefault(FName(*Name), GetResult);
				bSuccess = GetResult == EMetaSoundBuilderResult::Succeeded;
				if (!bSuccess)
				{
					Error = FString::Printf(TEXT("MetaSound graph input '%s' was not found"), *Name);
				}
			}
		}

		ItemResult->SetBoolField(TEXT("success"), bSuccess);
		ItemResult->SetBoolField(TEXT("changed"), bSuccess && !bDryRun);
		if (!Error.IsEmpty())
		{
			ItemResult->SetStringField(TEXT("error"), Error);
		}
		Results.Add(MakeShareable(new FJsonValueObject(ItemResult)));

		if (bSuccess)
		{
			++Succeeded;
			bAnyChanged = bAnyChanged || !bDryRun;
		}
		else
		{
			++Failed;
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				TSharedPtr<FJsonObject> Partial = MakeShareable(new FJsonObject);
				Partial->SetStringField(TEXT("tool"), GetToolName());
				Partial->SetArrayField(TEXT("results"), Results);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_METASOUND_GRAPH_EDIT_FAILED"), Error, nullptr, Partial);
			}
		}
	}

	if (!bDryRun && bAnyChanged)
	{
		FString FinalizeError;
		if (!MetaSoundToolUtils::BuildExistingSource(Source, Builder, FinalizeError))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
				Transaction.Reset();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_METASOUND_BUILD_FAILED"), FinalizeError);
		}
		if (bSave)
		{
			MetaSoundToolUtils::SaveAsset(Source, Warnings);
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), Defaults->Num());
	Summary->SetNumberField(TEXT("succeeded"), Succeeded);
	Summary->SetNumberField(TEXT("failed"), Failed);

	TSharedPtr<FJsonObject> Response = MetaSoundToolUtils::SerializeSourceSummary(Source, false);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), Results);
	Response->SetObjectField(TEXT("summary"), Summary);
	Response->SetBoolField(TEXT("saved"), !bDryRun && bSave && Warnings.Num() == 0);
	Response->SetArrayField(TEXT("warnings"), Warnings);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
