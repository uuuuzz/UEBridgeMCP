#include "Tools/Blueprint/ApplyBlueprintFixupsTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Blueprint/BlueprintCompileUtils.h"
#include "Tools/Blueprint/BlueprintFindingUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "Engine/Blueprint.h"
#include "ScopedTransaction.h"

namespace
{
	FString InferFixupErrorCode(const FString& Action, const FString& ErrorMessage)
	{
		if (ErrorMessage.Contains(TEXT("required"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_MISSING_REQUIRED_FIELD");
		}

		if (ErrorMessage.Contains(TEXT("Unsupported action"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_INVALID_ACTION");
		}

		if (Action == TEXT("recompile_dependencies") || ErrorMessage.Contains(TEXT("compile"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR");
		}

		return TEXT("UEBMCP_BLUEPRINT_FIXUP_FAILED");
	}

	void AppendUniqueModifiedAssets(
		const TArray<TSharedPtr<FJsonValue>>& SourceAssets,
		TSet<FString>& InOutAssetSet)
	{
		for (const TSharedPtr<FJsonValue>& AssetValue : SourceAssets)
		{
			if (AssetValue.IsValid())
			{
				InOutAssetSet.Add(AssetValue->AsString());
			}
		}
	}
}

FString UApplyBlueprintFixupsTool::GetToolDescription() const
{
	return TEXT("Apply safe, structural Blueprint fixups such as refreshing nodes, removing orphan pins, recompiling dependencies, and conforming implemented interfaces.");
}

TMap<FString, FMcpSchemaProperty> UApplyBlueprintFixupsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> ActionSchema = MakeShared<FMcpSchemaProperty>();
	ActionSchema->Type = TEXT("object");
	ActionSchema->NestedRequired = {TEXT("action")};
	ActionSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Fixup action"),
		{
			TEXT("refresh_all_nodes"),
			TEXT("reconstruct_invalid_nodes"),
			TEXT("remove_orphan_pins"),
			TEXT("recompile_dependencies"),
			TEXT("conform_implemented_interfaces")
		},
		true)));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Ordered Blueprint fixup actions");
	ActionsSchema.Items = ActionSchema;
	ActionsSchema.bRequired = true;
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Compile policy after fixups: 'never' or 'final'"), {TEXT("never"), TEXT("final")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after successful fixups")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate request shape without persisting edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Rollback on the first action or compile failure (default true)")));
	return Schema;
}

TArray<FString> UApplyBlueprintFixupsTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("actions")};
}

FMcpToolResult UApplyBlueprintFixupsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("final"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	if (!CompilePolicy.Equals(TEXT("never"), ESearchCase::IgnoreCase)
		&& !CompilePolicy.Equals(TEXT("final"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("'compile' must be 'never' or 'final'"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	TArray<TSharedPtr<FJsonValue>> AggregatedDiagnostics;
	TSet<FString> ModifiedAssetSet;
	bool bAnyFailed = false;

	if (bDryRun)
	{
		for (int32 Index = 0; Index < ActionsArray->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* ActionObject = nullptr;
			if (!(*ActionsArray)[Index]->TryGetObject(ActionObject) || !ActionObject || !(*ActionObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("Each action must be a valid object"));
			}

			const FString Action = GetStringArgOrDefault(*ActionObject, TEXT("action"));
			if (!BlueprintCompileUtils::IsSupportedFixupAction(Action))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), FString::Printf(TEXT("Unsupported action '%s'"), *Action));
			}

			TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
			ResultObject->SetNumberField(TEXT("index"), Index);
			ResultObject->SetStringField(TEXT("action"), Action);
			ResultObject->SetBoolField(TEXT("success"), true);
			ResultObject->SetStringField(TEXT("status"), TEXT("dry_run"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		}

		BlueprintFindingUtils::FQuery FindingQuery;
		FindingQuery.AssetPath = AssetPath;
		FindingQuery.SessionId = Context.SessionId;
		TArray<TSharedPtr<FJsonValue>> FindingsAfter = BlueprintFindingUtils::CollectFindings(Blueprint, FindingQuery);

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), TEXT("apply-blueprint-fixups"));
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("status"), TEXT("dry_run"));
		Response->SetStringField(TEXT("asset_path"), AssetPath);
		Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
		Response->SetArrayField(TEXT("results"), ResultsArray);
		Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		Response->SetObjectField(TEXT("compile"), BlueprintCompileUtils::MakeCompileReportJson(BlueprintCompileUtils::FCompileReport()));
		Response->SetArrayField(TEXT("findings_after"), FindingsAfter);
		Response->SetArrayField(TEXT("diagnostics"), AggregatedDiagnostics);
		Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
		return FMcpToolResult::StructuredJson(Response);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Apply Blueprint Fixups")));
	FMcpAssetModifier::MarkModified(Blueprint);

	for (int32 Index = 0; Index < ActionsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!(*ActionsArray)[Index]->TryGetObject(ActionObject) || !ActionObject || !(*ActionObject).IsValid())
		{
			if (bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("Each action must be a valid object"));
		}

		const FString Action = GetStringArgOrDefault(*ActionObject, TEXT("action"));
		TSharedPtr<FJsonObject> ActionResult = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> ActionDiagnostics;
		TArray<TSharedPtr<FJsonValue>> ActionModifiedAssets;
		FString ActionError;

		const bool bActionSuccess = BlueprintCompileUtils::ApplyFixupAction(
			Blueprint,
			Action,
			AssetPath,
			Context.SessionId,
			100,
			ActionResult,
			ActionError,
			ActionDiagnostics,
			ActionModifiedAssets);

		if (!ActionResult.IsValid())
		{
			ActionResult = MakeShareable(new FJsonObject);
			ActionResult->SetStringField(TEXT("action"), Action);
		}

		ActionResult->SetNumberField(TEXT("index"), Index);
		ActionResult->SetBoolField(TEXT("success"), bActionSuccess);

		for (const TSharedPtr<FJsonValue>& DiagnosticValue : ActionDiagnostics)
		{
			AggregatedDiagnostics.Add(DiagnosticValue);
		}
		AppendUniqueModifiedAssets(ActionModifiedAssets, ModifiedAssetSet);

		if (!bActionSuccess)
		{
			const FString ErrorCode = InferFixupErrorCode(Action, ActionError);
			ActionResult->SetStringField(TEXT("error"), ActionError);
			ActionResult->SetStringField(TEXT("code"), ErrorCode);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
			bAnyFailed = true;

			if (bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();

				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("asset_path"), AssetPath);
				Details->SetNumberField(TEXT("failed_action_index"), Index);
				Details->SetStringField(TEXT("action"), Action);

				TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
				PartialResult->SetArrayField(TEXT("results"), ResultsArray);
				PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
				return FMcpToolResult::StructuredError(ErrorCode, ActionError, Details, PartialResult);
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
	}

	BlueprintCompileUtils::FCompileReport CompileReport;
	if (CompilePolicy.Equals(TEXT("final"), ESearchCase::IgnoreCase))
	{
		BlueprintCompileUtils::CompileBlueprintWithReport(Blueprint, AssetPath, Context.SessionId, 100, CompileReport);
		for (const TSharedPtr<FJsonValue>& DiagnosticValue : CompileReport.Diagnostics)
		{
			AggregatedDiagnostics.Add(DiagnosticValue);
		}

		if (!CompileReport.bSuccess)
		{
			bAnyFailed = true;
			TSharedPtr<FJsonObject> CompilePartial = MakeShareable(new FJsonObject);
			CompilePartial->SetStringField(TEXT("stage"), TEXT("compile"));
			CompilePartial->SetBoolField(TEXT("success"), false);
			CompilePartial->SetStringField(TEXT("error"), CompileReport.ErrorMessage);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(CompilePartial)));

			if (bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();

				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("asset_path"), AssetPath);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR"), CompileReport.ErrorMessage, Details);
			}
		}
	}

	if (!bAnyFailed && bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
		{
			if (bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();
			}
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("asset_path"), AssetPath);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError, Details);
		}
	}

	BlueprintFindingUtils::FQuery FindingQuery;
	FindingQuery.AssetPath = AssetPath;
	FindingQuery.SessionId = Context.SessionId;
	TArray<TSharedPtr<FJsonValue>> FindingsAfter = BlueprintFindingUtils::CollectFindings(Blueprint, FindingQuery);

	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<FString> SortedModifiedAssets = ModifiedAssetSet.Array();
	SortedModifiedAssets.Sort();
	for (const FString& ModifiedAsset : SortedModifiedAssets)
	{
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(ModifiedAsset)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("apply-blueprint-fixups"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("compile"), BlueprintCompileUtils::MakeCompileReportJson(CompileReport));
	Response->SetArrayField(TEXT("findings_after"), FindingsAfter);
	Response->SetArrayField(TEXT("diagnostics"), AggregatedDiagnostics);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
