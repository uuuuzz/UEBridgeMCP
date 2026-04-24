// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/SourceControlAssetsTool.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SourceControlOperations.h"
#include "Serialization/JsonSerializer.h"

namespace SourceControlAssetsToolPrivate
{
	bool ResolveAssetPathToFilePath(const FString& InAssetPath, FString& OutPackageName, FString& OutFilePath, FString& OutError)
	{
		if (InAssetPath.IsEmpty())
		{
			OutError = TEXT("Asset path is empty");
			return false;
		}

		if (InAssetPath.EndsWith(TEXT(".uasset")) || InAssetPath.EndsWith(TEXT(".umap")))
		{
			OutPackageName = TEXT("");
			OutFilePath = InAssetPath;
			return true;
		}

		OutPackageName = FPackageName::ObjectPathToPackageName(InAssetPath);
		if (OutPackageName.IsEmpty())
		{
			OutPackageName = InAssetPath;
		}

		if (!FPackageName::DoesPackageExist(OutPackageName, &OutFilePath))
		{
			const FString AssetFilename = FPackageName::LongPackageNameToFilename(OutPackageName, FPackageName::GetAssetPackageExtension());
			const FString MapFilename = FPackageName::LongPackageNameToFilename(OutPackageName, FPackageName::GetMapPackageExtension());
			if (FPaths::FileExists(AssetFilename))
			{
				OutFilePath = AssetFilename;
			}
			else if (FPaths::FileExists(MapFilename))
			{
				OutFilePath = MapFilename;
			}
			else
			{
				OutError = FString::Printf(TEXT("Could not resolve source control file path for '%s'"), *InAssetPath);
				return false;
			}
		}

		return true;
	}

	bool ResolveAssetPaths(
		const TArray<TSharedPtr<FJsonValue>>& InAssetValues,
		TArray<FString>& OutAssetPaths,
		TArray<FString>& OutPackageNames,
		TArray<FString>& OutFilePaths,
		FString& OutError)
	{
		for (const TSharedPtr<FJsonValue>& AssetValue : InAssetValues)
		{
			if (!AssetValue.IsValid())
			{
				continue;
			}

			const FString AssetPath = AssetValue->AsString();
			FString PackageName;
			FString FilePath;
			if (!ResolveAssetPathToFilePath(AssetPath, PackageName, FilePath, OutError))
			{
				return false;
			}

			OutAssetPaths.Add(AssetPath);
			OutPackageNames.Add(PackageName);
			OutFilePaths.Add(FilePath);
		}

		if (OutFilePaths.Num() == 0)
		{
			OutError = TEXT("No valid asset paths were provided");
			return false;
		}

		return true;
	}

	FString GetProviderStatusText(ISourceControlProvider& Provider)
	{
		const FString StatusText = Provider.GetStatusText().ToString();
		return StatusText.IsEmpty() ? TEXT("Source control operation failed") : StatusText;
	}
}

FString USourceControlAssetsTool::GetToolDescription() const
{
	return TEXT("Source control operations on assets: status, checkout, revert, submit, and sync. Uses the editor's configured source control provider.");
}

TMap<FString, FMcpSchemaProperty> USourceControlAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FMcpSchemaProperty> ActionItemSchema = MakeShared<FMcpSchemaProperty>();
	ActionItemSchema->Type = TEXT("object");
	ActionItemSchema->Description = TEXT("Source control action descriptor");
	ActionItemSchema->NestedRequired = {TEXT("action"), TEXT("asset_paths")};
	ActionItemSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Source control action"),
		{TEXT("status"), TEXT("checkout"), TEXT("revert"), TEXT("submit"), TEXT("sync")},
		true)));
	ActionItemSchema->Properties.Add(TEXT("asset_paths"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(
		TEXT("Asset object paths or package file paths"), TEXT("string"), true)));
	ActionItemSchema->Properties.Add(TEXT("description"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"), TEXT("Submit description for submit actions"))));
	ActionItemSchema->Properties.Add(TEXT("keep_checked_out"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("boolean"), TEXT("Keep files checked out after submit"))));
	ActionItemSchema->Properties.Add(TEXT("revision"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"), TEXT("Optional revision for sync actions"))));
	ActionItemSchema->Properties.Add(TEXT("head_revision"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("boolean"), TEXT("Sync to head revision"))));
	ActionItemSchema->Properties.Add(TEXT("force"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("boolean"), TEXT("Force sync even if the file already appears current"))));
	ActionItemSchema->Properties.Add(TEXT("last_synced"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("boolean"), TEXT("Force sync to the last synced revision"))));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Array of source control actions with nested action-specific fields.");
	ActionsSchema.bRequired = true;
	ActionsSchema.Items = ActionItemSchema;
	Schema.Add(TEXT("actions"), ActionsSchema);

	return Schema;
}

TArray<FString> USourceControlAssetsTool::GetRequiredParams() const
{
	return {TEXT("actions")};
}

FMcpToolResult USourceControlAssetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	if (!SourceControlModule.IsEnabled() || !SourceControlModule.GetProvider().IsAvailable())
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_SOURCE_CONTROL_PROVIDER_UNAVAILABLE"),
			TEXT("Source control provider is not available or not enabled"));
	}

	ISourceControlProvider& Provider = SourceControlModule.GetProvider();

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	int32 SucceededCount = 0;
	int32 FailedCount = 0;

	for (int32 ActionIndex = 0; ActionIndex < ActionsArray->Num(); ++ActionIndex)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!(*ActionsArray)[ActionIndex]->TryGetObject(ActionObject) || !(*ActionObject).IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), ActionIndex);

		FString ActionName;
		(*ActionObject)->TryGetStringField(TEXT("action"), ActionName);
		ResultObject->SetStringField(TEXT("action"), ActionName);

		const TArray<TSharedPtr<FJsonValue>>* AssetValues = nullptr;
		if (!(*ActionObject)->TryGetArrayField(TEXT("asset_paths"), AssetValues) || !AssetValues || AssetValues->Num() == 0)
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("'asset_paths' is required"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			FailedCount++;
			continue;
		}

		TArray<FString> AssetPaths;
		TArray<FString> PackageNames;
		TArray<FString> FilePaths;
		FString ResolveError;
		if (!SourceControlAssetsToolPrivate::ResolveAssetPaths(*AssetValues, AssetPaths, PackageNames, FilePaths, ResolveError))
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), ResolveError);
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			FailedCount++;
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> AssetPathArray;
		for (const FString& AssetPath : AssetPaths)
		{
			AssetPathArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
		}
		ResultObject->SetArrayField(TEXT("asset_paths"), AssetPathArray);

		bool bActionSuccess = false;
		FString ActionError;

		if (ActionName == TEXT("status"))
		{
			TArray<TSharedPtr<FJsonValue>> StatusArray;
			for (int32 PathIndex = 0; PathIndex < FilePaths.Num(); ++PathIndex)
			{
				const FString& FilePath = FilePaths[PathIndex];
				const FString& AssetPath = AssetPaths[PathIndex];
				const FString& PackageName = PackageNames[PathIndex];
				FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::ForceUpdate);

				TSharedPtr<FJsonObject> StatusObject = MakeShareable(new FJsonObject);
				StatusObject->SetStringField(TEXT("asset_path"), AssetPath);
				if (!PackageName.IsEmpty())
				{
					StatusObject->SetStringField(TEXT("package_name"), PackageName);
				}
				StatusObject->SetStringField(TEXT("file_path"), FilePath);
				StatusObject->SetBoolField(TEXT("state_valid"), State.IsValid());

				if (State.IsValid())
				{
					StatusObject->SetBoolField(TEXT("is_checked_out"), State->IsCheckedOut());
					StatusObject->SetBoolField(TEXT("is_current"), State->IsCurrent());
					StatusObject->SetBoolField(TEXT("is_added"), State->IsAdded());
					StatusObject->SetBoolField(TEXT("is_deleted"), State->IsDeleted());
					StatusObject->SetBoolField(TEXT("is_source_controlled"), State->IsSourceControlled());
					StatusObject->SetBoolField(TEXT("is_unknown"), State->IsUnknown());
					StatusObject->SetBoolField(TEXT("can_checkout"), State->CanCheckout());
					StatusObject->SetBoolField(TEXT("can_checkin"), State->CanCheckIn());
					StatusObject->SetBoolField(TEXT("can_revert"), State->CanRevert());
					StatusObject->SetBoolField(TEXT("can_add"), State->CanAdd());
					StatusObject->SetStringField(TEXT("display_name"), State->GetDisplayName().ToString());
					StatusObject->SetStringField(TEXT("tooltip"), State->GetDisplayTooltip().ToString());
					const TOptional<FText> StatusText = State->GetStatusText();
					if (StatusText.IsSet())
					{
						StatusObject->SetStringField(TEXT("status_text"), StatusText.GetValue().ToString());
					}
				}

				StatusArray.Add(MakeShareable(new FJsonValueObject(StatusObject)));
			}

			ResultObject->SetArrayField(TEXT("statuses"), StatusArray);
			bActionSuccess = true;
		}
		else if (ActionName == TEXT("checkout"))
		{
			const ECommandResult::Type CommandResult = Provider.Execute(ISourceControlOperation::Create<FCheckOut>(), FilePaths);
			bActionSuccess = CommandResult == ECommandResult::Succeeded;
			if (!bActionSuccess)
			{
				ActionError = SourceControlAssetsToolPrivate::GetProviderStatusText(Provider);
			}
		}
		else if (ActionName == TEXT("revert"))
		{
			const ECommandResult::Type CommandResult = Provider.Execute(ISourceControlOperation::Create<FRevert>(), FilePaths);
			bActionSuccess = CommandResult == ECommandResult::Succeeded;
			if (!bActionSuccess)
			{
				ActionError = SourceControlAssetsToolPrivate::GetProviderStatusText(Provider);
			}
		}
		else if (ActionName == TEXT("submit"))
		{
			FString Description;
			if (!(*ActionObject)->TryGetStringField(TEXT("description"), Description) || Description.IsEmpty())
			{
				ActionError = TEXT("'description' is required for submit");
			}
			else
			{
				const bool bKeepCheckedOut = GetBoolArgOrDefault(*ActionObject, TEXT("keep_checked_out"), false);
				TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOperation = ISourceControlOperation::Create<FCheckIn>();
				CheckInOperation->SetDescription(FText::FromString(Description));
				CheckInOperation->SetKeepCheckedOut(bKeepCheckedOut);

				const ECommandResult::Type CommandResult = Provider.Execute(CheckInOperation, FilePaths);
				bActionSuccess = CommandResult == ECommandResult::Succeeded;
				if (bActionSuccess)
				{
					ResultObject->SetStringField(TEXT("description"), Description);
					const FString SuccessMessage = CheckInOperation->GetSuccessMessage().ToString();
					if (!SuccessMessage.IsEmpty())
					{
						ResultObject->SetStringField(TEXT("provider_message"), SuccessMessage);
					}
				}
				else
				{
					ActionError = SourceControlAssetsToolPrivate::GetProviderStatusText(Provider);
				}
			}
		}
		else if (ActionName == TEXT("sync"))
		{
			TSharedRef<FSync, ESPMode::ThreadSafe> SyncOperation = ISourceControlOperation::Create<FSync>();

			const FString Revision = GetStringArgOrDefault(*ActionObject, TEXT("revision"));
			if (!Revision.IsEmpty())
			{
				SyncOperation->SetRevision(Revision);
			}
			if (GetBoolArgOrDefault(*ActionObject, TEXT("head_revision"), false))
			{
				SyncOperation->SetHeadRevisionFlag(true);
			}
			if (GetBoolArgOrDefault(*ActionObject, TEXT("force"), false))
			{
				SyncOperation->SetForce(true);
			}
			if (GetBoolArgOrDefault(*ActionObject, TEXT("last_synced"), false))
			{
				SyncOperation->SetLastSyncedFlag(true);
			}

			const ECommandResult::Type CommandResult = Provider.Execute(SyncOperation, FilePaths);
			bActionSuccess = CommandResult == ECommandResult::Succeeded;
			if (!bActionSuccess)
			{
				ActionError = SourceControlAssetsToolPrivate::GetProviderStatusText(Provider);
			}
		}
		else
		{
			ActionError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		ResultObject->SetBoolField(TEXT("success"), bActionSuccess);
		if (!bActionSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), ActionError);
			bAnyFailed = true;
			FailedCount++;
		}
		else
		{
			SucceededCount++;
			if (ActionName != TEXT("status"))
			{
				for (const FString& AssetPath : AssetPaths)
				{
					ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
				}
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("total"), ActionsArray->Num());
	SummaryObject->SetNumberField(TEXT("succeeded"), SucceededCount);
	SummaryObject->SetNumberField(TEXT("failed"), FailedCount);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("source-control-assets"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), SummaryObject);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}