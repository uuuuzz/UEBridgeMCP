// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Blueprint/BlueprintCompileUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"

FString UCompileAssetsTool::GetToolDescription() const
{
	return TEXT("Compile one or more Blueprint assets (Blueprint, WidgetBlueprint, AnimBlueprint). "
		"Returns per-asset compile status and diagnostics. This is the recommended compile path; "
		"use run-python-script only as a legacy fallback.");
}

TMap<FString, FMcpSchemaProperty> UCompileAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_paths"), FMcpSchemaProperty::MakeArray(
		TEXT("One or more compileable asset paths"), TEXT("string"), true));

	Schema.Add(TEXT("mode"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile mode: 'auto' detects asset type, 'blueprint_only' skips non-Blueprint assets"),
		{TEXT("auto"), TEXT("blueprint_only")}));

	Schema.Add(TEXT("save_before_compile"), FMcpSchemaProperty::Make(TEXT("boolean"),
		TEXT("Save packages before compiling")));

	Schema.Add(TEXT("include_diagnostics"), FMcpSchemaProperty::Make(TEXT("boolean"),
		TEXT("Include detailed compile diagnostics (default true)")));

	Schema.Add(TEXT("max_diagnostics"), FMcpSchemaProperty::Make(TEXT("integer"),
		TEXT("Max diagnostics per asset (default 100)")));

	Schema.Add(TEXT("stop_on_first_error"), FMcpSchemaProperty::Make(TEXT("boolean"),
		TEXT("Stop batch on first compile failure")));

	return Schema;
}

TArray<FString> UCompileAssetsTool::GetRequiredParams() const
{
	return {TEXT("asset_paths")};
}

FMcpToolResult UCompileAssetsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("asset_paths"), PathsArray) || !PathsArray || PathsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_paths' array is required"));
	}

	const FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("auto"));
	const bool bBlueprintOnly = Mode.Equals(TEXT("blueprint_only"), ESearchCase::IgnoreCase);
	const bool bSaveBeforeCompile = GetBoolArgOrDefault(Arguments, TEXT("save_before_compile"), false);
	const bool bIncludeDiagnostics = GetBoolArgOrDefault(Arguments, TEXT("include_diagnostics"), true);
	const int32 MaxDiagnostics = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_diagnostics"), 100));
	const bool bStopOnFirstError = GetBoolArgOrDefault(Arguments, TEXT("stop_on_first_error"), false);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> AggregatedDiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	int32 TotalSucceeded = 0;
	int32 TotalFailed = 0;

	for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
	{
		const FString AssetPath = PathValue->AsString();
		TSharedPtr<FJsonObject> AssetResult = MakeShareable(new FJsonObject);
		AssetResult->SetStringField(TEXT("asset_path"), AssetPath);

		FString LoadError;
		UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
		if (!Blueprint)
		{
			TArray<TSharedPtr<FJsonValue>> AssetDiagnosticsArray;
			AssetResult->SetStringField(TEXT("asset_type"), TEXT("Unknown"));
			AssetResult->SetBoolField(TEXT("compiled"), false);
			AssetResult->SetBoolField(TEXT("success"), false);
			AssetResult->SetStringField(TEXT("error"), LoadError);
			AssetResult->SetNumberField(TEXT("warning_count"), 0);
			AssetResult->SetNumberField(TEXT("error_count"), 1);
			if (bIncludeDiagnostics && MaxDiagnostics > 0)
			{
				TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
				Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
				Diagnostic->SetStringField(TEXT("code"), TEXT("UEBMCP_ASSET_NOT_FOUND"));
				Diagnostic->SetStringField(TEXT("message"), LoadError);
				Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
				AssetDiagnosticsArray.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
				AggregatedDiagnosticsArray.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
			}
			AssetResult->SetArrayField(TEXT("diagnostics"), AssetDiagnosticsArray);
			ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			TotalFailed++;

			if (bStopOnFirstError)
			{
				break;
			}
			continue;
		}

		FString AssetType = TEXT("Blueprint");
		if (Blueprint->IsA<UAnimBlueprint>())
		{
			AssetType = TEXT("AnimBlueprint");
		}
		else if (Blueprint->GetClass()->GetName().Contains(TEXT("WidgetBlueprint")))
		{
			AssetType = TEXT("WidgetBlueprint");
		}
		AssetResult->SetStringField(TEXT("asset_type"), AssetType);

		if (bBlueprintOnly && AssetType != TEXT("Blueprint"))
		{
			AssetResult->SetBoolField(TEXT("compiled"), false);
			AssetResult->SetBoolField(TEXT("success"), true);
			AssetResult->SetBoolField(TEXT("skipped"), true);
			AssetResult->SetStringField(TEXT("skip_reason"), TEXT("Skipped because mode=blueprint_only and asset is not a plain Blueprint"));
			AssetResult->SetNumberField(TEXT("warning_count"), 0);
			AssetResult->SetNumberField(TEXT("error_count"), 0);
			AssetResult->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
			ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			TotalSucceeded++;
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
		int32 WarningCount = 0;
		int32 ErrorCount = 0;

		if (bSaveBeforeCompile)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
			{
				AssetResult->SetBoolField(TEXT("compiled"), false);
				AssetResult->SetBoolField(TEXT("success"), false);
				AssetResult->SetStringField(TEXT("error"), SaveError);
				ErrorCount = 1;

				if (bIncludeDiagnostics && MaxDiagnostics > 0)
				{
					TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
					Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
					Diagnostic->SetStringField(TEXT("code"), TEXT("UEBMCP_ASSET_SAVE_FAILED"));
					Diagnostic->SetStringField(TEXT("message"), SaveError);
					Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
					DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
				}

				AssetResult->SetNumberField(TEXT("warning_count"), WarningCount);
				AssetResult->SetNumberField(TEXT("error_count"), ErrorCount);
				AssetResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
				ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
				for (const TSharedPtr<FJsonValue>& DiagnosticValue : DiagnosticsArray)
				{
					AggregatedDiagnosticsArray.Add(DiagnosticValue);
				}
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
				TotalFailed++;

				if (bStopOnFirstError)
				{
					break;
				}
				continue;
			}
		}

		const BlueprintCompileUtils::FCompileReport CompileReport = [&]()
		{
			BlueprintCompileUtils::FCompileReport Report;
			BlueprintCompileUtils::CompileBlueprintWithReport(Blueprint, AssetPath, Context.SessionId, MaxDiagnostics, Report);
			return Report;
		}();
		const bool bCompileSuccess = CompileReport.bSuccess;
		AssetResult->SetBoolField(TEXT("compiled"), true);
		AssetResult->SetBoolField(TEXT("success"), bCompileSuccess);
		DiagnosticsArray = bIncludeDiagnostics ? CompileReport.Diagnostics : TArray<TSharedPtr<FJsonValue>>();
		WarningCount = CompileReport.WarningCount;
		ErrorCount = CompileReport.ErrorCount;

		if (!bCompileSuccess)
		{
			AssetResult->SetStringField(TEXT("error"), CompileReport.ErrorMessage);
			if (!bIncludeDiagnostics && MaxDiagnostics > 0)
			{
				TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
				Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
				Diagnostic->SetStringField(TEXT("code"), TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR"));
				Diagnostic->SetStringField(TEXT("message"), CompileReport.ErrorMessage);
				Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
			}
		}
		else if (!bIncludeDiagnostics)
		{
			if (WarningCount > 0 && MaxDiagnostics > 0)
			{
				TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
				Diagnostic->SetStringField(TEXT("severity"), TEXT("warning"));
				Diagnostic->SetStringField(TEXT("code"), TEXT("UEBMCP_BLUEPRINT_COMPILE_WARNING"));
				Diagnostic->SetStringField(TEXT("message"), TEXT("Blueprint compiled with warnings"));
				Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
			}
		}

		if (MaxDiagnostics >= 0 && DiagnosticsArray.Num() > MaxDiagnostics)
		{
			DiagnosticsArray.SetNum(MaxDiagnostics);
		}

		AssetResult->SetNumberField(TEXT("warning_count"), WarningCount);
		AssetResult->SetNumberField(TEXT("error_count"), ErrorCount);
		AssetResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));

		for (const TSharedPtr<FJsonValue>& DiagnosticValue : DiagnosticsArray)
		{
			AggregatedDiagnosticsArray.Add(DiagnosticValue);
		}

		if (bCompileSuccess)
		{
			ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
			TotalSucceeded++;
			if (WarningCount > 0)
			{
				WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Asset '%s' compiled with warnings"), *AssetPath))));
			}
		}
		else
		{
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(AssetResult)));
			TotalFailed++;
			if (bStopOnFirstError)
			{
				break;
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("compile-assets"));
	Response->SetBoolField(TEXT("success"), TotalFailed == 0);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), AggregatedDiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), PathsArray->Num());
	Summary->SetNumberField(TEXT("succeeded"), TotalSucceeded);
	Summary->SetNumberField(TEXT("failed"), TotalFailed);
	Response->SetObjectField(TEXT("summary"), Summary);

	return FMcpToolResult::StructuredJson(Response, TotalFailed > 0);
}
