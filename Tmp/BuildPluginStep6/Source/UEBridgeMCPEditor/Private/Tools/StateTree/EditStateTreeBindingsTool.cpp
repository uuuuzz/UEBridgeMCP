// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/StateTree/EditStateTreeBindingsTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "PropertyBindingBindingCollection.h"
#include "PropertyBindingPath.h"
#include "ScopedTransaction.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorPropertyBindings.h"

namespace
{
	bool ParseGuidString(const FString& InValue, FGuid& OutGuid)
	{
		return !InValue.IsEmpty() && FGuid::Parse(InValue, OutGuid);
	}

	bool ParsePropertyBindingPath(const FString& StructIdString, const FString& PathString, FPropertyBindingPath& OutPath, FString& OutError)
	{
		FGuid StructId;
		if (!ParseGuidString(StructIdString, StructId))
		{
			OutError = FString::Printf(TEXT("Invalid struct id '%s'"), *StructIdString);
			return false;
		}

		OutPath = FPropertyBindingPath(StructId);
		if (PathString.IsEmpty())
		{
			return true;
		}

		TArray<FString> Segments;
		PathString.ParseIntoArray(Segments, TEXT("."), true);
		for (const FString& SegmentString : Segments)
		{
			FString NamePart = SegmentString;
			int32 ArrayIndex = INDEX_NONE;

			int32 BracketIndex = INDEX_NONE;
			if (SegmentString.FindChar(TEXT('['), BracketIndex))
			{
				const int32 EndBracketIndex = SegmentString.Find(TEXT("]"), ESearchCase::IgnoreCase, ESearchDir::FromStart, BracketIndex);
				if (EndBracketIndex == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Invalid property path segment '%s'"), *SegmentString);
					return false;
				}

				NamePart = SegmentString.Left(BracketIndex);
				ArrayIndex = FCString::Atoi(*SegmentString.Mid(BracketIndex + 1, EndBracketIndex - BracketIndex - 1));
			}

			if (NamePart.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Invalid property path segment '%s'"), *SegmentString);
				return false;
			}

			OutPath.AddPathSegment(FPropertyBindingPathSegment(FName(*NamePart), ArrayIndex));
		}

		return true;
	}

	TSharedPtr<FJsonObject> SerializeBinding(const FStateTreePropertyPathBinding& Binding)
	{
		TSharedPtr<FJsonObject> BindingObject = MakeShareable(new FJsonObject);
		BindingObject->SetStringField(TEXT("source_struct_id"), Binding.GetSourcePath().GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower));
		BindingObject->SetStringField(TEXT("source_path"), Binding.GetSourcePath().ToString());
		BindingObject->SetStringField(TEXT("target_struct_id"), Binding.GetTargetPath().GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower));
		BindingObject->SetStringField(TEXT("target_path"), Binding.GetTargetPath().ToString());
		return BindingObject;
	}
}

FString UEditStateTreeBindingsTool::GetToolDescription() const
{
	return TEXT("Edit StateTree property bindings using explicit struct ids and property paths.");
}

TMap<FString, FMcpSchemaProperty> UEditStateTreeBindingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("StateTree asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("StateTree binding operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Binding action"),
		{ TEXT("add_binding"), TEXT("remove_binding"), TEXT("clear_bindings_for_struct") },
		true)));
	OperationSchema->Properties.Add(TEXT("source_struct_id"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source struct guid for add_binding"))));
	OperationSchema->Properties.Add(TEXT("source_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source property path for add_binding"))));
	OperationSchema->Properties.Add(TEXT("target_struct_id"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target struct guid"))));
	OperationSchema->Properties.Add(TEXT("target_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target property path"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("StateTree binding operations");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the StateTree asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UEditStateTreeBindingsTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditStateTreeBindingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UStateTree* StateTree = FMcpAssetModifier::LoadAssetByPath<UStateTree>(AssetPath, LoadError);
	if (!StateTree)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (!EditorData)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SUBSYSTEM_UNAVAILABLE"), TEXT("StateTree editor data is not available"));
	}

	FStateTreeEditorPropertyBindings* EditorBindings = EditorData->GetPropertyEditorBindings();
	if (!EditorBindings)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SUBSYSTEM_UNAVAILABLE"), TEXT("StateTree property bindings are not available"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit StateTree Bindings")));
		StateTree->Modify();
		EditorData->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString ActionName;
		(*OperationObject)->TryGetStringField(TEXT("action"), ActionName);
		ResultObject->SetStringField(TEXT("action"), ActionName);

		bool bOperationSuccess = false;
		bool bOperationChanged = false;
		FString OperationError;

		if (ActionName == TEXT("add_binding"))
		{
			FString SourceStructIdString;
			FString SourcePathString;
			FString TargetStructIdString;
			FString TargetPathString;
			if (!(*OperationObject)->TryGetStringField(TEXT("source_struct_id"), SourceStructIdString)
				|| !(*OperationObject)->TryGetStringField(TEXT("source_path"), SourcePathString)
				|| !(*OperationObject)->TryGetStringField(TEXT("target_struct_id"), TargetStructIdString)
				|| !(*OperationObject)->TryGetStringField(TEXT("target_path"), TargetPathString))
			{
				OperationError = TEXT("'source_struct_id', 'source_path', 'target_struct_id', and 'target_path' are required for add_binding");
			}
			else
			{
				FPropertyBindingPath SourcePath;
				FPropertyBindingPath TargetPath;
				if (!ParsePropertyBindingPath(SourceStructIdString, SourcePathString, SourcePath, OperationError)
					|| !ParsePropertyBindingPath(TargetStructIdString, TargetPathString, TargetPath, OperationError))
				{
				}
				else
				{
					bOperationChanged = true;
					if (!bDryRun)
					{
						EditorBindings->AddBinding(SourcePath, TargetPath);
					}
					ResultObject->SetStringField(TEXT("source_struct_id"), SourceStructIdString);
					ResultObject->SetStringField(TEXT("source_path"), SourcePathString);
					ResultObject->SetStringField(TEXT("target_struct_id"), TargetStructIdString);
					ResultObject->SetStringField(TEXT("target_path"), TargetPathString);
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("remove_binding"))
		{
			FString TargetStructIdString;
			FString TargetPathString;
			if (!(*OperationObject)->TryGetStringField(TEXT("target_struct_id"), TargetStructIdString)
				|| !(*OperationObject)->TryGetStringField(TEXT("target_path"), TargetPathString))
			{
				OperationError = TEXT("'target_struct_id' and 'target_path' are required for remove_binding");
			}
			else
			{
				FPropertyBindingPath TargetPath;
				if (!ParsePropertyBindingPath(TargetStructIdString, TargetPathString, TargetPath, OperationError))
				{
				}
				else
				{
					bOperationChanged = EditorBindings->HasBinding(TargetPath, FPropertyBindingBindingCollection::ESearchMode::Exact);
					if (!bDryRun && bOperationChanged)
					{
						EditorBindings->RemoveBindings(TargetPath, FPropertyBindingBindingCollection::ESearchMode::Exact);
					}
					ResultObject->SetStringField(TEXT("target_struct_id"), TargetStructIdString);
					ResultObject->SetStringField(TEXT("target_path"), TargetPathString);
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("clear_bindings_for_struct"))
		{
			FString TargetStructIdString;
			if (!(*OperationObject)->TryGetStringField(TEXT("target_struct_id"), TargetStructIdString))
			{
				OperationError = TEXT("'target_struct_id' is required for clear_bindings_for_struct");
			}
			else
			{
				FGuid TargetStructId;
				if (!ParseGuidString(TargetStructIdString, TargetStructId))
				{
					OperationError = FString::Printf(TEXT("Invalid target_struct_id '%s'"), *TargetStructIdString);
				}
				else
				{
					bOperationChanged = false;
					for (const FStateTreePropertyPathBinding& Binding : EditorBindings->GetBindings())
					{
						if (Binding.GetTargetPath().GetStructID() == TargetStructId)
						{
							bOperationChanged = true;
							break;
						}
					}

					if (!bDryRun && bOperationChanged)
					{
						EditorBindings->RemoveBindings([TargetStructId](FPropertyBindingBinding& Binding)
							{
								return Binding.GetTargetPath().GetStructID() == TargetStructId;
							});
					}
					ResultObject->SetStringField(TEXT("target_struct_id"), TargetStructIdString);
					bOperationSuccess = true;
				}
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!bOperationSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), OperationError);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					OperationError,
					nullptr,
					GameplayToolUtils::BuildBatchFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}
		else
		{
			bChanged = bChanged || bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(StateTree);
#if WITH_EDITOR
		StateTree->CompileIfChanged();
#endif
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && bSave && bChanged)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(StateTree, false, SaveError))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TArray<TSharedPtr<FJsonValue>> BindingArray;
	for (const FStateTreePropertyPathBinding& Binding : EditorBindings->GetBindings())
	{
		BindingArray.Add(MakeShareable(new FJsonValueObject(SerializeBinding(Binding))));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetArrayField(TEXT("bindings"), BindingArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
