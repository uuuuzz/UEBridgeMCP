// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/StaticMesh/EditStaticMeshSettingsTool.h"

#include "Utils/McpAssetModifier.h"

#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshEditorSubsystem.h"

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

	bool FloatArraysEqual(const TArray<float>& Left, const TArray<float>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!FMath::IsNearlyEqual(Left[Index], Right[Index]))
			{
				return false;
			}
		}

		return true;
	}
}

FString UEditStaticMeshSettingsTool::GetToolDescription() const
{
	return TEXT("Transactional static mesh settings editing for LOD group, LOD screen sizes, and Nanite enablement.");
}

TMap<FString, FMcpSchemaProperty> UEditStaticMeshSettingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Static mesh asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Static mesh settings operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Static mesh settings action"),
		{ TEXT("set_lod_group"), TEXT("set_lod_screen_sizes"), TEXT("set_nanite_enabled") },
		true)));
	OperationSchema->Properties.Add(TEXT("lod_group"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("LOD group name"))));
	OperationSchema->Properties.Add(TEXT("lod_screen_sizes"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("LOD screen sizes"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("enabled"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Nanite enabled flag"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Static mesh settings operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the static mesh asset")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));

	return Schema;
}

TArray<FString> UEditStaticMeshSettingsTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditStaticMeshSettingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
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
	UStaticMesh* StaticMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(AssetPath, LoadError);
	if (!StaticMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UStaticMeshEditorSubsystem* StaticMeshSubsystem = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
	if (!StaticMeshSubsystem)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SUBSYSTEM_UNAVAILABLE"), TEXT("StaticMeshEditorSubsystem is not available"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Static Mesh Settings")));
		FMcpAssetModifier::MarkModified(StaticMesh);
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bMeshChanged = false;

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

		if (ActionName == TEXT("set_lod_group"))
		{
			FString LodGroup;
			if (!(*OperationObject)->TryGetStringField(TEXT("lod_group"), LodGroup))
			{
				OperationError = TEXT("'lod_group' is required for set_lod_group");
			}
			else
			{
				bOperationChanged = !StaticMeshSubsystem->GetLODGroup(StaticMesh).IsEqual(FName(*LodGroup));
				ResultObject->SetStringField(TEXT("lod_group"), LodGroup);
				if (!bDryRun && bOperationChanged)
				{
					if (!StaticMeshSubsystem->SetLODGroup(StaticMesh, FName(*LodGroup), true))
					{
						OperationError = TEXT("Failed to set LOD group");
					}
				}
				if (OperationError.IsEmpty())
				{
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("set_lod_screen_sizes"))
		{
			const TArray<TSharedPtr<FJsonValue>>* ScreenSizesArray = nullptr;
			if (!(*OperationObject)->TryGetArrayField(TEXT("lod_screen_sizes"), ScreenSizesArray) || !ScreenSizesArray || ScreenSizesArray->Num() == 0)
			{
				OperationError = TEXT("'lod_screen_sizes' array is required for set_lod_screen_sizes");
			}
			else
			{
				TArray<float> RequestedScreenSizes;
				TArray<TSharedPtr<FJsonValue>> ResultScreenSizes;
				for (const TSharedPtr<FJsonValue>& Value : *ScreenSizesArray)
				{
					const float ScreenSize = static_cast<float>(Value->AsNumber());
					RequestedScreenSizes.Add(ScreenSize);
					ResultScreenSizes.Add(MakeShareable(new FJsonValueNumber(ScreenSize)));
				}

				bOperationChanged = !FloatArraysEqual(StaticMeshSubsystem->GetLodScreenSizes(StaticMesh), RequestedScreenSizes);
				ResultObject->SetArrayField(TEXT("lod_screen_sizes"), ResultScreenSizes);
				if (!bDryRun && bOperationChanged && !StaticMeshSubsystem->SetLodScreenSizes(StaticMesh, RequestedScreenSizes))
				{
					OperationError = TEXT("Failed to set LOD screen sizes");
				}
				if (OperationError.IsEmpty())
				{
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("set_nanite_enabled"))
		{
			bool bEnabled = false;
			if (!(*OperationObject)->TryGetBoolField(TEXT("enabled"), bEnabled))
			{
				OperationError = TEXT("'enabled' is required for set_nanite_enabled");
			}
			else
			{
				FMeshNaniteSettings NaniteSettings = StaticMeshSubsystem->GetNaniteSettings(StaticMesh);
				bOperationChanged = NaniteSettings.bEnabled != bEnabled;
				ResultObject->SetBoolField(TEXT("enabled"), bEnabled);
				if (!bDryRun && bOperationChanged)
				{
					NaniteSettings.bEnabled = bEnabled;
					StaticMeshSubsystem->SetNaniteSettings(StaticMesh, NaniteSettings, true);
				}
				bOperationSuccess = true;
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
					BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}
		else
		{
			bMeshChanged = bMeshChanged || bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bMeshChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(StaticMesh);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && bSave && bMeshChanged)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(StaticMesh, false, SaveError))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
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
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
