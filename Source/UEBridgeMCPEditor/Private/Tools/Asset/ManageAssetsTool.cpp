// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/ManageAssetsTool.h"
#include "Utils/McpAssetModifier.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace ManageAssetsToolPrivate
{
	TArray<FString> JsonArrayToStringArray(const TArray<TSharedPtr<FJsonValue>>& InValues)
	{
		TArray<FString> Result;
		for (const TSharedPtr<FJsonValue>& Value : InValues)
		{
			if (Value.IsValid())
			{
				Result.Add(Value->AsString());
			}
		}
		return Result;
	}
}

FString UManageAssetsTool::GetToolDescription() const
{
	return TEXT("Manage assets: rename, move, duplicate, delete, save. Batched via 'actions' array.");
}

TMap<FString, FMcpSchemaProperty> UManageAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FMcpSchemaProperty> ActionItemSchema = MakeShared<FMcpSchemaProperty>();
	ActionItemSchema->Type = TEXT("object");
	ActionItemSchema->Description = TEXT("Asset management action");
	ActionItemSchema->NestedRequired = {TEXT("action")};
	ActionItemSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Asset management action"),
		{TEXT("rename"), TEXT("move"), TEXT("duplicate"), TEXT("delete"), TEXT("save"), TEXT("consolidate")},
		true)));
	ActionItemSchema->Properties.Add(TEXT("asset_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Primary asset path for rename/move/duplicate/delete/save"))));
	ActionItemSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New asset name for rename or duplicate"))));
	ActionItemSchema->Properties.Add(TEXT("destination_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination package path for move or duplicate"))));
	ActionItemSchema->Properties.Add(TEXT("target_asset_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target asset path for consolidate"))));
	ActionItemSchema->Properties.Add(TEXT("source_asset_paths"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Source asset paths for consolidate"), TEXT("string"))));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Array of asset lifecycle actions with nested action-specific fields.");
	ActionsSchema.bRequired = true;
	ActionsSchema.Items = ActionItemSchema;
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save modified assets after actions complete")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transaction label")));

	return Schema;
}

TArray<FString> UManageAssetsTool::GetRequiredParams() const
{
	return {TEXT("actions")};
}

FMcpToolResult UManageAssetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Manage Assets"));

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	int32 SucceededCount = 0;
	int32 FailedCount = 0;

	for (int32 Index = 0; Index < ActionsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!(*ActionsArray)[Index]->TryGetObject(ActionObject) || !(*ActionObject).IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActionResult = MakeShareable(new FJsonObject);
		FString ActionError;
		bool bSuccess = false;

		if (!bDryRun)
		{
			bSuccess = ExecuteAction(*ActionObject, Index, ActionResult, ActionError);
		}
		else
		{
			FString ActionName;
			(*ActionObject)->TryGetStringField(TEXT("action"), ActionName);
			ActionResult->SetStringField(TEXT("action"), ActionName);
			bSuccess = true;
		}

		ActionResult->SetNumberField(TEXT("index"), Index);
		ActionResult->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			ActionResult->SetStringField(TEXT("error"), ActionError);
			bAnyFailed = true;
			FailedCount++;
			if (bRollbackOnError && !bDryRun)
			{
				Transaction.Reset();
				TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
				PartialResult->SetStringField(TEXT("tool"), TEXT("manage-assets"));
				PartialResult->SetArrayField(TEXT("results"), ResultsArray);
				PartialResult->SetArrayField(TEXT("warnings"), WarningsArray);
				PartialResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
				PartialResult->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
				PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), ActionError, nullptr, PartialResult);
			}
		}
		else
		{
			SucceededCount++;
			FString ModifiedAssetPath;
			if (ActionResult->TryGetStringField(TEXT("asset_path"), ModifiedAssetPath) && !ModifiedAssetPath.IsEmpty())
			{
				ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(ModifiedAssetPath)));
			}
			if (ActionResult->TryGetStringField(TEXT("new_asset_path"), ModifiedAssetPath) && !ModifiedAssetPath.IsEmpty())
			{
				ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(ModifiedAssetPath)));
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
	}

	if (!bDryRun && bSave && !bAnyFailed)
	{
		for (const TSharedPtr<FJsonValue>& ResultValue : ResultsArray)
		{
			const TSharedPtr<FJsonObject>* ResultObject = nullptr;
			if (!ResultValue.IsValid() || !ResultValue->TryGetObject(ResultObject) || !(*ResultObject).IsValid())
			{
				continue;
			}

			FString AssetPath;
			if (!(*ResultObject)->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
			{
				if (!(*ResultObject)->TryGetStringField(TEXT("new_asset_path"), AssetPath) || AssetPath.IsEmpty())
				{
					continue;
				}
			}

			FString LoadError;
			UObject* Asset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
			if (!Asset)
			{
				WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Failed to load asset for save: %s"), *AssetPath))));
				continue;
			}

			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Asset, false, SaveError))
			{
				WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Failed to save asset '%s': %s"), *AssetPath, *SaveError))));
			}
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), ActionsArray->Num());
	Summary->SetNumberField(TEXT("succeeded"), SucceededCount);
	Summary->SetNumberField(TEXT("failed"), FailedCount);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("manage-assets"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

bool UManageAssetsTool::ExecuteAction(const TSharedPtr<FJsonObject>& Action, int32 Index, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
{
	FString ActionName;
	if (!Action->TryGetStringField(TEXT("action"), ActionName))
	{
		OutError = TEXT("Missing 'action'");
		return false;
	}

	OutResult->SetStringField(TEXT("action"), ActionName);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	if (ActionName == TEXT("consolidate"))
	{
		FString TargetAssetPath;
		const TArray<TSharedPtr<FJsonValue>>* SourcePathsArray = nullptr;
		if (!Action->TryGetStringField(TEXT("target_asset_path"), TargetAssetPath))
		{
			OutError = TEXT("'target_asset_path' required");
			return false;
		}
		if (!Action->TryGetArrayField(TEXT("source_asset_paths"), SourcePathsArray) || !SourcePathsArray || SourcePathsArray->Num() == 0)
		{
			OutError = TEXT("'source_asset_paths' required");
			return false;
		}

		FString LoadError;
		UObject* TargetAsset = FMcpAssetModifier::LoadAssetByPath(TargetAssetPath, LoadError);
		if (!TargetAsset)
		{
			OutError = LoadError;
			return false;
		}

		TArray<UObject*> SourceAssets;
		TArray<TSharedPtr<FJsonValue>> SourceAssetValues;
		for (const TSharedPtr<FJsonValue>& SourcePathValue : *SourcePathsArray)
		{
			const FString SourceAssetPath = SourcePathValue->AsString();
			UObject* SourceAsset = FMcpAssetModifier::LoadAssetByPath(SourceAssetPath, LoadError);
			if (!SourceAsset)
			{
				OutError = LoadError;
				return false;
			}
			SourceAssets.Add(SourceAsset);
			SourceAssetValues.Add(MakeShareable(new FJsonValueString(SourceAssetPath)));
		}

		UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		if (!EditorAssetSubsystem)
		{
			OutError = TEXT("EditorAssetSubsystem is not available");
			return false;
		}

		if (!EditorAssetSubsystem->ConsolidateAssets(TargetAsset, SourceAssets))
		{
			OutError = TEXT("Consolidate failed");
			return false;
		}

		OutResult->SetStringField(TEXT("target_asset_path"), TargetAssetPath);
		OutResult->SetArrayField(TEXT("source_asset_paths"), SourceAssetValues);
		OutResult->SetStringField(TEXT("asset_path"), TargetAssetPath);
		return true;
	}

	FString AssetPath;
	if (!Action->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("Missing 'asset_path'");
		return false;
	}

	OutResult->SetStringField(TEXT("asset_path"), AssetPath);

	if (ActionName == TEXT("rename"))
	{
		FString NewName;
		if (!Action->TryGetStringField(TEXT("new_name"), NewName)) { OutError = TEXT("'new_name' required"); return false; }

		FString LoadError;
		UObject* Asset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!Asset) { OutError = LoadError; return false; }

		TArray<FAssetRenameData> RenameData;
		FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		RenameData.Add(FAssetRenameData(Asset, PackagePath, NewName));
		bool bResult = AssetTools.RenameAssets(RenameData);
		if (!bResult) { OutError = TEXT("Rename failed"); return false; }
		OutResult->SetStringField(TEXT("new_name"), NewName);
		OutResult->SetStringField(TEXT("new_asset_path"), FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *NewName, *NewName));
		return true;
	}
	else if (ActionName == TEXT("move"))
	{
		FString DestinationPath;
		if (!Action->TryGetStringField(TEXT("destination_path"), DestinationPath)) { OutError = TEXT("'destination_path' required"); return false; }

		FString LoadError;
		UObject* Asset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!Asset) { OutError = LoadError; return false; }

		const FString AssetName = FPackageName::GetShortName(AssetPath);
		TArray<FAssetRenameData> RenameData;
		RenameData.Add(FAssetRenameData(Asset, DestinationPath, AssetName));
		bool bResult = AssetTools.RenameAssets(RenameData);
		if (!bResult) { OutError = TEXT("Move failed"); return false; }
		OutResult->SetStringField(TEXT("destination_path"), DestinationPath);
		OutResult->SetStringField(TEXT("new_asset_path"), FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *AssetName, *AssetName));
		return true;
	}
	else if (ActionName == TEXT("duplicate"))
	{
		FString DestinationPath;
		if (!Action->TryGetStringField(TEXT("destination_path"), DestinationPath)) { OutError = TEXT("'destination_path' required"); return false; }

		const FString AssetName = FPackageName::GetShortName(AssetPath);
		FString NewName;
		Action->TryGetStringField(TEXT("new_name"), NewName);
		if (NewName.IsEmpty()) { NewName = AssetName + TEXT("_Copy"); }

		FString LoadError;
		UObject* SourceAsset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!SourceAsset) { OutError = LoadError; return false; }

		UObject* DuplicatedAsset = AssetTools.DuplicateAsset(NewName, DestinationPath, SourceAsset);
		if (!DuplicatedAsset) { OutError = TEXT("Duplicate failed"); return false; }
		OutResult->SetStringField(TEXT("new_asset_path"), DuplicatedAsset->GetPathName());
		return true;
	}
	else if (ActionName == TEXT("delete"))
	{
		FString LoadError;
		UObject* Asset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!Asset) { OutError = LoadError; return false; }

		TArray<UObject*> ObjectsToDelete;
		ObjectsToDelete.Add(Asset);
		int32 Deleted = ObjectTools::DeleteObjects(ObjectsToDelete, false);
		if (Deleted == 0) { OutError = TEXT("Delete failed"); return false; }
		return true;
	}
	else if (ActionName == TEXT("save"))
	{
		FString LoadError;
		UObject* Asset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!Asset) { OutError = LoadError; return false; }

		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Asset, false, SaveError)) { OutError = SaveError; return false; }
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
	return false;
}
