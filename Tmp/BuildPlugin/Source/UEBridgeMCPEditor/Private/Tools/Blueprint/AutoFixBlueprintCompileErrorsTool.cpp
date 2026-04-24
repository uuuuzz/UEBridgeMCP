// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/AutoFixBlueprintCompileErrorsTool.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Serialization/JsonSerializer.h"

namespace AutoFixBlueprintCompileErrorsToolPrivate
{
	bool IsSupportedStrategy(const FString& Strategy)
	{
		return Strategy == TEXT("refresh_all_nodes")
			|| Strategy == TEXT("reconstruct_invalid_nodes")
			|| Strategy == TEXT("remove_orphan_pins")
			|| Strategy == TEXT("recompile_dependencies");
	}

	TSharedPtr<FJsonObject> MakeDiagnostic(const FString& Severity, const FString& Code, const FString& Message, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
		Diagnostic->SetStringField(TEXT("severity"), Severity);
		Diagnostic->SetStringField(TEXT("code"), Code);
		Diagnostic->SetStringField(TEXT("message"), Message);
		Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
		return Diagnostic;
	}

	bool ApplyRefreshAllNodes(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutStrategyResult)
	{
		FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);

		OutStrategyResult = MakeShareable(new FJsonObject);
		OutStrategyResult->SetStringField(TEXT("strategy"), TEXT("refresh_all_nodes"));
		OutStrategyResult->SetBoolField(TEXT("success"), true);
		return true;
	}

	bool ApplyReconstructInvalidNodes(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutStrategyResult)
	{
		int32 ReconstructedNodeCount = 0;
		TArray<UEdGraph*> AllGraphs;
		FMcpAssetModifier::GetAllSearchableGraphs(Blueprint, AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				if (Node->HasDeprecatedReference())
				{
					Node->Modify();
					Node->ReconstructNode();
					ReconstructedNodeCount++;
				}
			}
		}

		OutStrategyResult = MakeShareable(new FJsonObject);
		OutStrategyResult->SetStringField(TEXT("strategy"), TEXT("reconstruct_invalid_nodes"));
		OutStrategyResult->SetBoolField(TEXT("success"), true);
		OutStrategyResult->SetNumberField(TEXT("reconstructed_nodes"), ReconstructedNodeCount);
		return true;
	}

	bool ApplyRemoveOrphanPins(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutStrategyResult)
	{
		int32 RemovedPinCount = 0;
		TArray<UEdGraph*> AllGraphs;
		FMcpAssetModifier::GetAllSearchableGraphs(Blueprint, AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				TArray<UEdGraphPin*> PinsToRemove;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->bOrphanedPin)
					{
						PinsToRemove.Add(Pin);
					}
				}

				for (UEdGraphPin* Pin : PinsToRemove)
				{
					Node->Modify();
					Node->RemovePin(Pin);
					RemovedPinCount++;
				}
			}
		}

		OutStrategyResult = MakeShareable(new FJsonObject);
		OutStrategyResult->SetStringField(TEXT("strategy"), TEXT("remove_orphan_pins"));
		OutStrategyResult->SetBoolField(TEXT("success"), true);
		OutStrategyResult->SetNumberField(TEXT("removed_orphan_pins"), RemovedPinCount);
		return true;
	}

	bool ApplyRecompileDependencies(
		UBlueprint* Blueprint,
		TSharedPtr<FJsonObject>& OutStrategyResult,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		TArray<UBlueprint*> DependentBlueprints;
		FBlueprintEditorUtils::GetDependentBlueprints(Blueprint, DependentBlueprints);

		TArray<TSharedPtr<FJsonValue>> DependencyResults;
		bool bAllDependenciesCompiled = true;
		for (UBlueprint* DependentBlueprint : DependentBlueprints)
		{
			if (!DependentBlueprint)
			{
				continue;
			}

			FMcpAssetModifier::RefreshBlueprintNodes(DependentBlueprint);

			FString CompileError;
			const bool bCompileSuccess = FMcpAssetModifier::CompileBlueprint(DependentBlueprint, CompileError);

			TSharedPtr<FJsonObject> DependencyResult = MakeShareable(new FJsonObject);
			DependencyResult->SetStringField(TEXT("asset_path"), DependentBlueprint->GetPathName());
			DependencyResult->SetBoolField(TEXT("success"), bCompileSuccess);
			if (!bCompileSuccess)
			{
				DependencyResult->SetStringField(TEXT("error"), CompileError);
				OutDiagnostics.Add(MakeShareable(new FJsonValueObject(MakeDiagnostic(
					TEXT("error"),
					TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"),
					CompileError,
					DependentBlueprint->GetPathName()))));
				bAllDependenciesCompiled = false;
			}
			else
			{
				OutModifiedAssets.Add(MakeShareable(new FJsonValueString(DependentBlueprint->GetPathName())));
			}

			DependencyResults.Add(MakeShareable(new FJsonValueObject(DependencyResult)));
		}

		OutStrategyResult = MakeShareable(new FJsonObject);
		OutStrategyResult->SetStringField(TEXT("strategy"), TEXT("recompile_dependencies"));
		OutStrategyResult->SetBoolField(TEXT("success"), bAllDependenciesCompiled);
		OutStrategyResult->SetArrayField(TEXT("dependencies"), DependencyResults);
		return bAllDependenciesCompiled;
	}

	bool ApplyStrategy(
		UBlueprint* Blueprint,
		const FString& Strategy,
		TSharedPtr<FJsonObject>& OutStrategyResult,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		if (Strategy == TEXT("refresh_all_nodes"))
		{
			return ApplyRefreshAllNodes(Blueprint, OutStrategyResult);
		}
		if (Strategy == TEXT("reconstruct_invalid_nodes"))
		{
			return ApplyReconstructInvalidNodes(Blueprint, OutStrategyResult);
		}
		if (Strategy == TEXT("remove_orphan_pins"))
		{
			return ApplyRemoveOrphanPins(Blueprint, OutStrategyResult);
		}
		if (Strategy == TEXT("recompile_dependencies"))
		{
			return ApplyRecompileDependencies(Blueprint, OutStrategyResult, OutDiagnostics, OutModifiedAssets);
		}

		return false;
	}
}

FString UAutoFixBlueprintCompileErrorsTool::GetToolDescription() const
{
	return TEXT("Attempt to automatically fix Blueprint compile errors using deterministic strategies: "
		"refresh_all_nodes, reconstruct_invalid_nodes, remove_orphan_pins, and recompile_dependencies. "
		"Conservative v1 with diagnostics, partial results, and no semantic graph rewrites.");
}

TMap<FString, FMcpSchemaProperty> UAutoFixBlueprintCompileErrorsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_paths"), FMcpSchemaProperty::MakeArray(
		TEXT("Blueprint asset paths to fix"), TEXT("string"), true));

	FMcpSchemaProperty StrategiesSchema;
	StrategiesSchema.Type = TEXT("array");
	StrategiesSchema.Description = TEXT("Fix strategies to apply: 'refresh_all_nodes', 'reconstruct_invalid_nodes', 'remove_orphan_pins', 'recompile_dependencies'");
	StrategiesSchema.ItemsType = TEXT("string");
	Schema.Add(TEXT("strategies"), StrategiesSchema);

	Schema.Add(TEXT("max_attempts"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Max fix+compile attempts (default 3)")));
	Schema.Add(TEXT("compile_after_each_attempt"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile after each attempt (default true). If false, only one effective attempt is used.")));
	Schema.Add(TEXT("save_on_success"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save fixed Blueprints when compilation succeeds")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));

	return Schema;
}

TArray<FString> UAutoFixBlueprintCompileErrorsTool::GetRequiredParams() const
{
	return {TEXT("asset_paths")};
}

FMcpToolResult UAutoFixBlueprintCompileErrorsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("asset_paths"), PathsArray) || !PathsArray || PathsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_paths' required"));
	}

	const int32 MaxAttempts = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("max_attempts"), 3));
	const bool bCompileAfterEach = GetBoolArgOrDefault(Arguments, TEXT("compile_after_each_attempt"), true);
	const bool bSaveOnSuccess = GetBoolArgOrDefault(Arguments, TEXT("save_on_success"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	TArray<FString> Strategies;
	const TArray<TSharedPtr<FJsonValue>>* StrategiesArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("strategies"), StrategiesArray) && StrategiesArray && StrategiesArray->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& StrategyValue : *StrategiesArray)
		{
			const FString Strategy = StrategyValue->AsString();
			if (!AutoFixBlueprintCompileErrorsToolPrivate::IsSupportedStrategy(Strategy))
			{
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Unsupported strategy '%s'"), *Strategy));
			}
			Strategies.Add(Strategy);
		}
	}
	else
	{
		Strategies = {TEXT("refresh_all_nodes"), TEXT("reconstruct_invalid_nodes"), TEXT("remove_orphan_pins")};
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	int32 SucceededCount = 0;
	int32 FailedCount = 0;

	if (!bCompileAfterEach && MaxAttempts > 1)
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("compile_after_each_attempt=false limits the tool to a single effective attempt"))));
	}

	const int32 EffectiveAttemptCount = bCompileAfterEach ? MaxAttempts : 1;

	for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
	{
		const FString AssetPath = PathValue->AsString();
		TSharedPtr<FJsonObject> AssetResult = MakeShareable(new FJsonObject);
		AssetResult->SetStringField(TEXT("asset_path"), AssetPath);

		if (bDryRun)
		{
			TArray<TSharedPtr<FJsonValue>> PlannedStrategies;
			for (const FString& Strategy : Strategies)
			{
				PlannedStrategies.Add(MakeShareable(new FJsonValueString(Strategy)));
			}
			AssetResult->SetBoolField(TEXT("success"), true);
			AssetResult->SetStringField(TEXT("status"), TEXT("dry_run"));
			AssetResult->SetArrayField(TEXT("planned_strategies"), PlannedStrategies);
			ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			SucceededCount++;
			continue;
		}

		FString LoadError;
		UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
		if (!Blueprint)
		{
			AssetResult->SetBoolField(TEXT("success"), false);
			AssetResult->SetStringField(TEXT("error"), LoadError);
			ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(AutoFixBlueprintCompileErrorsToolPrivate::MakeDiagnostic(
				TEXT("error"),
				TEXT("UEBMCP_ASSET_NOT_FOUND"),
				LoadError,
				AssetPath))));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			bAnyFailed = true;
			FailedCount++;
			continue;
		}

		bool bAssetFixed = false;
		bool bStrategyFailure = false;
		TArray<TSharedPtr<FJsonValue>> AttemptArray;
		int32 AttemptsUsed = 0;

		for (int32 AttemptIndex = 0; AttemptIndex < EffectiveAttemptCount; ++AttemptIndex)
		{
			AttemptsUsed = AttemptIndex + 1;
			TSharedPtr<FJsonObject> AttemptObject = MakeShareable(new FJsonObject);
			AttemptObject->SetNumberField(TEXT("attempt"), AttemptIndex + 1);

			TArray<TSharedPtr<FJsonValue>> StrategyResults;
			bool bAttemptStrategySuccess = true;
			for (const FString& Strategy : Strategies)
			{
				TSharedPtr<FJsonObject> StrategyResult;
				const bool bStrategySuccess = AutoFixBlueprintCompileErrorsToolPrivate::ApplyStrategy(
					Blueprint,
					Strategy,
					StrategyResult,
					DiagnosticsArray,
					ModifiedAssetsArray);
				if (StrategyResult.IsValid())
				{
					StrategyResults.Add(MakeShareable(new FJsonValueObject(StrategyResult)));
				}
				bAttemptStrategySuccess = bAttemptStrategySuccess && bStrategySuccess;
			}
			AttemptObject->SetArrayField(TEXT("strategy_results"), StrategyResults);

			FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);
			FString CompileError;
			const bool bCompileSuccess = FMcpAssetModifier::CompileBlueprint(Blueprint, CompileError);
			AttemptObject->SetBoolField(TEXT("compile_attempted"), true);
			AttemptObject->SetBoolField(TEXT("compile_success"), bCompileSuccess);
			if (!bCompileSuccess)
			{
				AttemptObject->SetStringField(TEXT("compile_error"), CompileError);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(AutoFixBlueprintCompileErrorsToolPrivate::MakeDiagnostic(
					TEXT("error"),
					TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"),
					CompileError,
					AssetPath))));
			}
			else if (Blueprint->Status == BS_UpToDateWithWarnings)
			{
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(AutoFixBlueprintCompileErrorsToolPrivate::MakeDiagnostic(
					TEXT("warning"),
					TEXT("UEBMCP_BLUEPRINT_COMPILE_WARNING"),
					TEXT("Blueprint compiled with warnings after auto-fix"),
					AssetPath))));
			}

			AttemptArray.Add(MakeShareable(new FJsonValueObject(AttemptObject)));

			if (bCompileSuccess && bAttemptStrategySuccess)
			{
				bAssetFixed = true;
				break;
			}

			bStrategyFailure = bStrategyFailure || !bAttemptStrategySuccess;
		}

		AssetResult->SetBoolField(TEXT("success"), bAssetFixed);
		AssetResult->SetNumberField(TEXT("attempts_used"), AttemptsUsed);
		AssetResult->SetArrayField(TEXT("attempts"), AttemptArray);
		if (bStrategyFailure && !bAssetFixed)
		{
			AssetResult->SetStringField(TEXT("error"), TEXT("One or more repair strategies failed or dependencies still do not compile"));
		}
		else if (!bAssetFixed)
		{
			AssetResult->SetStringField(TEXT("error"), TEXT("Blueprint still does not compile after the requested attempts"));
		}

		if (bAssetFixed)
		{
			ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
			if (bSaveOnSuccess)
			{
				FString SaveError;
				if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
				{
					AssetResult->SetBoolField(TEXT("success"), false);
					AssetResult->SetStringField(TEXT("error"), SaveError);
					DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(AutoFixBlueprintCompileErrorsToolPrivate::MakeDiagnostic(
						TEXT("error"),
						TEXT("UEBMCP_ASSET_SAVE_FAILED"),
						SaveError,
						AssetPath))));
					PartialResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
					bAnyFailed = true;
					FailedCount++;
					ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
					continue;
				}
			}

			SucceededCount++;
		}
		else
		{
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			bAnyFailed = true;
			FailedCount++;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
	}

	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("total"), PathsArray->Num());
	SummaryObject->SetNumberField(TEXT("succeeded"), SucceededCount);
	SummaryObject->SetNumberField(TEXT("failed"), FailedCount);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("auto-fix-blueprint-compile-errors"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), SummaryObject);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
