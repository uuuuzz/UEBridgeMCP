// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/RunProjectMaintenanceChecksTool.h"

#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Asset/QueryUnusedAssetsTool.h"
#include "Tools/Project/QueryWorkspaceHealthTool.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> CopyStringArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(FieldName, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid())
				{
					Result.Add(MakeShareable(new FJsonValueString(Value->AsString())));
				}
			}
		}
		return Result;
	}

	TSharedPtr<FJsonObject> MakeCheck(
		const FString& Name,
		const FString& Status,
		const bool bSuccess,
		const FString& Message,
		const TSharedPtr<FJsonObject>& Payload = nullptr)
	{
		TSharedPtr<FJsonObject> Check = MakeShareable(new FJsonObject);
		Check->SetStringField(TEXT("name"), Name);
		Check->SetStringField(TEXT("status"), Status);
		Check->SetBoolField(TEXT("success"), bSuccess);
		Check->SetStringField(TEXT("message"), Message);
		if (Payload.IsValid())
		{
			Check->SetObjectField(TEXT("payload"), Payload);
		}
		return Check;
	}

	double ReadNumberField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, double DefaultValue = 0.0)
	{
		double Value = DefaultValue;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(FieldName, Value);
		}
		return Value;
	}
}

FString URunProjectMaintenanceChecksTool::GetToolDescription() const
{
	return TEXT("Run a curated project maintenance check bundle: workspace health, conservative unused assets, and optional Blueprint compile checks.");
}

TMap<FString, FMcpSchemaProperty> URunProjectMaintenanceChecksTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("include_workspace_health"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Run query-workspace-health. Default: true.")));
	Schema.Add(TEXT("include_unused_assets"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Run query-unused-assets. Default: true.")));
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Root content path for unused asset scan. Default: /Game.")));
	Schema.Add(TEXT("unused_asset_limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum unused asset candidates. Default: 25.")));
	Schema.Add(TEXT("compile_asset_paths"), FMcpSchemaProperty::MakeArray(TEXT("Optional Blueprint asset paths to compile as part of the check."), TEXT("string")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Plan checks without running compile checks. Read-only checks may still run.")));
	return Schema;
}

FMcpToolResult URunProjectMaintenanceChecksTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const bool bIncludeWorkspaceHealth = GetBoolArgOrDefault(Arguments, TEXT("include_workspace_health"), true);
	const bool bIncludeUnusedAssets = GetBoolArgOrDefault(Arguments, TEXT("include_unused_assets"), true);
	const FString RootPath = GetStringArgOrDefault(Arguments, TEXT("path"), TEXT("/Game"));
	const int32 UnusedAssetLimit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("unused_asset_limit"), 25), 1, 500);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const TArray<TSharedPtr<FJsonValue>> CompileAssetPaths = CopyStringArrayField(Arguments, TEXT("compile_asset_paths"));

	TArray<TSharedPtr<FJsonValue>> ChecksArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailure = false;

	if (bIncludeWorkspaceHealth)
	{
		TSharedPtr<FJsonObject> HealthArgs = MakeShareable(new FJsonObject);
		HealthArgs->SetBoolField(TEXT("include_dirty_packages"), true);
		HealthArgs->SetNumberField(TEXT("max_dirty_packages"), 25);

		UQueryWorkspaceHealthTool* HealthTool = NewObject<UQueryWorkspaceHealthTool>();
		const FMcpToolResult HealthResult = HealthTool->Execute(HealthArgs, Context);
		const TSharedPtr<FJsonObject> HealthPayload = HealthResult.GetStructuredContent();

		FString Status = TEXT("passed");
		FString Message = TEXT("Workspace health snapshot collected");
		const TSharedPtr<FJsonObject>* DirtyPackages = nullptr;
		if (HealthPayload.IsValid() && HealthPayload->TryGetObjectField(TEXT("dirty_packages"), DirtyPackages) && DirtyPackages && (*DirtyPackages).IsValid())
		{
			const int32 DirtyCount = FMath::RoundToInt(ReadNumberField(*DirtyPackages, TEXT("count")));
			if (DirtyCount > 0)
			{
				Status = TEXT("warning");
				Message = FString::Printf(TEXT("Workspace has %d dirty package(s)"), DirtyCount);
			}
		}

		if (!HealthResult.bSuccess)
		{
			bAnyFailure = true;
			Status = TEXT("failed");
			Message = TEXT("query-workspace-health failed");
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(HealthPayload.IsValid() ? HealthPayload : MakeShareable(new FJsonObject))));
		}

		ChecksArray.Add(MakeShareable(new FJsonValueObject(MakeCheck(TEXT("workspace_health"), Status, HealthResult.bSuccess, Message, HealthPayload))));
	}

	if (bIncludeUnusedAssets)
	{
		TSharedPtr<FJsonObject> UnusedArgs = MakeShareable(new FJsonObject);
		UnusedArgs->SetStringField(TEXT("path"), RootPath);
		UnusedArgs->SetNumberField(TEXT("limit"), UnusedAssetLimit);

		UQueryUnusedAssetsTool* UnusedAssetsTool = NewObject<UQueryUnusedAssetsTool>();
		const FMcpToolResult UnusedResult = UnusedAssetsTool->Execute(UnusedArgs, Context);
		const TSharedPtr<FJsonObject> UnusedPayload = UnusedResult.GetStructuredContent();

		FString Status = TEXT("passed");
		FString Message = TEXT("No conservative unused asset candidates returned");
		if (UnusedPayload.IsValid())
		{
			const int32 CandidateCount = FMath::RoundToInt(ReadNumberField(UnusedPayload, TEXT("count")));
			if (CandidateCount > 0)
			{
				Status = TEXT("warning");
				Message = FString::Printf(TEXT("%d conservative unused asset candidate(s) returned"), CandidateCount);
			}
		}

		if (!UnusedResult.bSuccess)
		{
			bAnyFailure = true;
			Status = TEXT("failed");
			Message = TEXT("query-unused-assets failed");
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(UnusedPayload.IsValid() ? UnusedPayload : MakeShareable(new FJsonObject))));
		}

		ChecksArray.Add(MakeShareable(new FJsonValueObject(MakeCheck(TEXT("unused_assets"), Status, UnusedResult.bSuccess, Message, UnusedPayload))));
	}

	if (CompileAssetPaths.Num() > 0)
	{
		if (bDryRun)
		{
			TSharedPtr<FJsonObject> PlannedPayload = MakeShareable(new FJsonObject);
			PlannedPayload->SetStringField(TEXT("tool_name"), TEXT("compile-assets"));
			PlannedPayload->SetArrayField(TEXT("asset_paths"), CompileAssetPaths);
			PlannedPayload->SetBoolField(TEXT("planned"), true);
			ChecksArray.Add(MakeShareable(new FJsonValueObject(MakeCheck(TEXT("blueprint_compile"), TEXT("planned"), true, TEXT("Compile check skipped because dry_run=true"), PlannedPayload))));
		}
		else
		{
			TSharedPtr<FJsonObject> CompileArgs = MakeShareable(new FJsonObject);
			CompileArgs->SetArrayField(TEXT("asset_paths"), CompileAssetPaths);
			CompileArgs->SetStringField(TEXT("mode"), TEXT("auto"));
			CompileArgs->SetBoolField(TEXT("include_diagnostics"), true);
			CompileArgs->SetNumberField(TEXT("max_diagnostics"), 50);

			UCompileAssetsTool* CompileTool = NewObject<UCompileAssetsTool>();
			const FMcpToolResult CompileResult = CompileTool->Execute(CompileArgs, Context);
			const TSharedPtr<FJsonObject> CompilePayload = CompileResult.GetStructuredContent();
			if (!CompileResult.bSuccess)
			{
				bAnyFailure = true;
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(CompilePayload.IsValid() ? CompilePayload : MakeShareable(new FJsonObject))));
			}
			ChecksArray.Add(MakeShareable(new FJsonValueObject(MakeCheck(
				TEXT("blueprint_compile"),
				CompileResult.bSuccess ? TEXT("passed") : TEXT("failed"),
				CompileResult.bSuccess,
				CompileResult.bSuccess ? TEXT("Blueprint compile check passed") : TEXT("Blueprint compile check failed"),
				CompilePayload))));
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total_checks"), ChecksArray.Num());
	Summary->SetBoolField(TEXT("dry_run"), bDryRun);
	Summary->SetBoolField(TEXT("success"), !bAnyFailure);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailure);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetArrayField(TEXT("checks"), ChecksArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredJson(Response, bAnyFailure);
}
