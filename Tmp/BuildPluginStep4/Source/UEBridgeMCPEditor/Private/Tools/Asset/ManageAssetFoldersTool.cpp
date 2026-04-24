// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/ManageAssetFoldersTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Utils/McpAssetModifier.h"

namespace
{
	TSharedPtr<FJsonObject> BuildPartialFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> PartialObject = MakeShareable(new FJsonObject);
		PartialObject->SetStringField(TEXT("tool"), ToolName);
		PartialObject->SetArrayField(TEXT("results"), ResultsArray);
		PartialObject->SetArrayField(TEXT("warnings"), WarningsArray);
		PartialObject->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		PartialObject->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		PartialObject->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return PartialObject;
	}

	bool IsValidFolderPath(const FString& FolderPath)
	{
		return !FolderPath.IsEmpty() && FolderPath.StartsWith(TEXT("/")) && !FolderPath.Contains(TEXT("."));
	}

	FString GetFolderLeafName(const FString& FolderPath)
	{
		FString Trimmed = FolderPath;
		Trimmed.RemoveFromEnd(TEXT("/"));

		FString Left;
		FString Right;
		if (Trimmed.Split(TEXT("/"), &Left, &Right, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			return Right;
		}
		return Trimmed;
	}

	FString GetFolderParentPath(const FString& FolderPath)
	{
		FString Trimmed = FolderPath;
		Trimmed.RemoveFromEnd(TEXT("/"));

		int32 SlashIndex = INDEX_NONE;
		if (Trimmed.FindLastChar(TEXT('/'), SlashIndex) && SlashIndex > 0)
		{
			return Trimmed.Left(SlashIndex);
		}
		return TEXT("/");
	}

	FString CombineFolderPath(const FString& ParentPath, const FString& LeafName)
	{
		if (ParentPath.EndsWith(TEXT("/")))
		{
			return ParentPath + LeafName;
		}
		return ParentPath + TEXT("/") + LeafName;
	}
}

FString UManageAssetFoldersTool::GetToolDescription() const
{
	return TEXT("Manage asset folders with batch operations for ensure, rename, move, and delete.");
}

TMap<FString, FMcpSchemaProperty> UManageAssetFoldersTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Asset folder operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Folder management action"),
		{ TEXT("ensure_folder"), TEXT("rename_folder"), TEXT("move_folder"), TEXT("delete_folder") },
		true)));
	OperationSchema->Properties.Add(TEXT("folder_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Folder path"), false)));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New folder name for rename_folder"))));
	OperationSchema->Properties.Add(TEXT("destination_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination parent folder for move_folder"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Folder operations");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));
	return Schema;
}

TArray<FString> UManageAssetFoldersTool::GetRequiredParams() const
{
	return { TEXT("operations") };
}

FMcpToolResult UManageAssetFoldersTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	if (!EditorAssetSubsystem)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SUBSYSTEM_UNAVAILABLE"), TEXT("EditorAssetSubsystem is not available"));
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Manage Asset Folders")));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

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

		FString FolderPath;
		(*OperationObject)->TryGetStringField(TEXT("folder_path"), FolderPath);

		if (!FolderPath.IsEmpty() && !IsValidFolderPath(FolderPath))
		{
			OperationError = FString::Printf(TEXT("Invalid folder_path '%s'"), *FolderPath);
		}
		else if (ActionName == TEXT("ensure_folder"))
		{
			if (FolderPath.IsEmpty())
			{
				OperationError = TEXT("'folder_path' is required for ensure_folder");
			}
			else
			{
				bOperationChanged = !EditorAssetSubsystem->DoesDirectoryExist(FolderPath);
				if (!bDryRun && bOperationChanged && !EditorAssetSubsystem->MakeDirectory(FolderPath))
				{
					OperationError = FString::Printf(TEXT("Failed to create folder '%s'"), *FolderPath);
				}
				bOperationSuccess = OperationError.IsEmpty();
				ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
			}
		}
		else if (ActionName == TEXT("rename_folder"))
		{
			FString NewName;
			if (!(*OperationObject)->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
			{
				OperationError = TEXT("'new_name' is required for rename_folder");
			}
			else if (NewName.Contains(TEXT("/")))
			{
				OperationError = TEXT("'new_name' must be a leaf folder name");
			}
			else if (!EditorAssetSubsystem->DoesDirectoryExist(FolderPath))
			{
				OperationError = FString::Printf(TEXT("Folder does not exist: %s"), *FolderPath);
			}
			else
			{
				const FString DestinationPath = CombineFolderPath(GetFolderParentPath(FolderPath), NewName);
				if (EditorAssetSubsystem->DoesDirectoryExist(DestinationPath))
				{
					OperationError = FString::Printf(TEXT("Destination folder already exists: %s"), *DestinationPath);
				}
				else
				{
					bOperationChanged = !FolderPath.Equals(DestinationPath, ESearchCase::IgnoreCase);
					if (!bDryRun && bOperationChanged && !EditorAssetSubsystem->RenameDirectory(FolderPath, DestinationPath))
					{
						OperationError = FString::Printf(TEXT("Failed to rename folder '%s' to '%s'"), *FolderPath, *DestinationPath);
					}
					bOperationSuccess = OperationError.IsEmpty();
					ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
					ResultObject->SetStringField(TEXT("new_folder_path"), DestinationPath);
				}
			}
		}
		else if (ActionName == TEXT("move_folder"))
		{
			FString DestinationPath;
			if (!(*OperationObject)->TryGetStringField(TEXT("destination_path"), DestinationPath) || !IsValidFolderPath(DestinationPath))
			{
				OperationError = TEXT("'destination_path' is required for move_folder");
			}
			else if (!EditorAssetSubsystem->DoesDirectoryExist(FolderPath))
			{
				OperationError = FString::Printf(TEXT("Folder does not exist: %s"), *FolderPath);
			}
			else
			{
				const FString TargetPath = CombineFolderPath(DestinationPath, GetFolderLeafName(FolderPath));
				if (!bDryRun && !EditorAssetSubsystem->DoesDirectoryExist(DestinationPath))
				{
					EditorAssetSubsystem->MakeDirectory(DestinationPath);
				}
				if (EditorAssetSubsystem->DoesDirectoryExist(TargetPath))
				{
					OperationError = FString::Printf(TEXT("Destination folder already exists: %s"), *TargetPath);
				}
				else
				{
					bOperationChanged = !FolderPath.Equals(TargetPath, ESearchCase::IgnoreCase);
					if (!bDryRun && bOperationChanged && !EditorAssetSubsystem->RenameDirectory(FolderPath, TargetPath))
					{
						OperationError = FString::Printf(TEXT("Failed to move folder '%s' to '%s'"), *FolderPath, *TargetPath);
					}
					bOperationSuccess = OperationError.IsEmpty();
					ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
					ResultObject->SetStringField(TEXT("new_folder_path"), TargetPath);
				}
			}
		}
		else if (ActionName == TEXT("delete_folder"))
		{
			if (FolderPath.IsEmpty())
			{
				OperationError = TEXT("'folder_path' is required for delete_folder");
			}
			else if (!EditorAssetSubsystem->DoesDirectoryExist(FolderPath))
			{
				bOperationSuccess = true;
				bOperationChanged = false;
				ResultObject->SetBoolField(TEXT("noop"), true);
			}
			else
			{
				const TArray<FString> AssetsInFolder = EditorAssetSubsystem->ListAssets(FolderPath, true, false);
				TArray<FString> SubPaths;
				AssetRegistry.GetSubPaths(FolderPath, SubPaths, true);
				if (AssetsInFolder.Num() > 0 || SubPaths.Num() > 0)
				{
					OperationError = FString::Printf(TEXT("Folder '%s' is not empty; recursive delete is not supported"), *FolderPath);
				}
				else
				{
					bOperationChanged = true;
					if (!bDryRun && !EditorAssetSubsystem->DeleteDirectory(FolderPath))
					{
						OperationError = FString::Printf(TEXT("Failed to delete folder '%s'"), *FolderPath);
					}
					bOperationSuccess = OperationError.IsEmpty();
					ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
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
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;

			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					OperationError,
					nullptr,
					BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}
		else if (bOperationChanged)
		{
			if (ResultObject->HasField(TEXT("new_folder_path")))
			{
				ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(ResultObject->GetStringField(TEXT("new_folder_path")))));
			}
			else if (!FolderPath.IsEmpty())
			{
				ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(FolderPath)));
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
