// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/EditMaterialInstanceTool.h"
#include "Utils/McpAssetModifier.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"

namespace EditMaterialInstanceToolPrivate
{
	bool RemoveNamedOverride(UMaterialInstanceConstant* MaterialInstance, const FName ParameterName)
	{
		const int32 RemovedScalar = MaterialInstance->ScalarParameterValues.RemoveAll(
			[ParameterName](const FScalarParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		const int32 RemovedVector = MaterialInstance->VectorParameterValues.RemoveAll(
			[ParameterName](const FVectorParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		const int32 RemovedDoubleVector = MaterialInstance->DoubleVectorParameterValues.RemoveAll(
			[ParameterName](const FDoubleVectorParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		const int32 RemovedTexture = MaterialInstance->TextureParameterValues.RemoveAll(
			[ParameterName](const FTextureParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		const int32 RemovedRuntimeVirtualTexture = MaterialInstance->RuntimeVirtualTextureParameterValues.RemoveAll(
			[ParameterName](const FRuntimeVirtualTextureParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		const int32 RemovedSparseVolumeTexture = MaterialInstance->SparseVolumeTextureParameterValues.RemoveAll(
			[ParameterName](const FSparseVolumeTextureParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		const int32 RemovedFont = MaterialInstance->FontParameterValues.RemoveAll(
			[ParameterName](const FFontParameterValue& ParameterValue)
			{
				return ParameterValue.ParameterInfo.Name == ParameterName;
			});

		return (RemovedScalar + RemovedVector + RemovedDoubleVector + RemovedTexture + RemovedRuntimeVirtualTexture + RemovedSparseVolumeTexture + RemovedFont) > 0;
	}

	bool ValidateAction(const TSharedPtr<FJsonObject>& ActionObject, FString& OutActionName, FString& OutParameterName, FString& OutError)
	{
		if (!ActionObject.IsValid())
		{
			OutError = TEXT("Action must be an object");
			return false;
		}

		ActionObject->TryGetStringField(TEXT("action"), OutActionName);
		ActionObject->TryGetStringField(TEXT("parameter"), OutParameterName);
		if (OutActionName.IsEmpty())
		{
			OutError = TEXT("Missing 'action' field");
			return false;
		}

		if (OutActionName == TEXT("set_scalar"))
		{
			if (OutParameterName.IsEmpty())
			{
				OutError = TEXT("'parameter' is required for set_scalar");
				return false;
			}
			double Value = 0.0;
			if (!ActionObject->TryGetNumberField(TEXT("value"), Value))
			{
				OutError = TEXT("'value' is required for set_scalar");
				return false;
			}
			return true;
		}

		if (OutActionName == TEXT("set_vector"))
		{
			if (OutParameterName.IsEmpty())
			{
				OutError = TEXT("'parameter' is required for set_vector");
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* ValueArray = nullptr;
			if (!ActionObject->TryGetArrayField(TEXT("value"), ValueArray) || !ValueArray || ValueArray->Num() < 4)
			{
				OutError = TEXT("'value' must be an array of 4 numbers for set_vector");
				return false;
			}
			return true;
		}

		if (OutActionName == TEXT("set_texture"))
		{
			if (OutParameterName.IsEmpty())
			{
				OutError = TEXT("'parameter' is required for set_texture");
				return false;
			}
			FString TexturePath;
			ActionObject->TryGetStringField(TEXT("texture_path"), TexturePath);
			if (TexturePath.IsEmpty())
			{
				OutError = TEXT("'texture_path' is required for set_texture");
				return false;
			}

			FString TextureError;
			if (!FMcpAssetModifier::LoadAssetByPath<UTexture>(TexturePath, TextureError))
			{
				OutError = TextureError;
				return false;
			}
			return true;
		}

		if (OutActionName == TEXT("clear_override"))
		{
			if (OutParameterName.IsEmpty())
			{
				OutError = TEXT("'parameter' is required for clear_override");
				return false;
			}
			return true;
		}

		if (OutActionName == TEXT("set_parent"))
		{
			FString ParentPath;
			ActionObject->TryGetStringField(TEXT("parent_path"), ParentPath);
			if (ParentPath.IsEmpty())
			{
				OutError = TEXT("'parent_path' is required for set_parent");
				return false;
			}

			FString ParentError;
			if (!FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(ParentPath, ParentError))
			{
				OutError = ParentError;
				return false;
			}
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported action: '%s'"), *OutActionName);
		return false;
	}
}

FString UEditMaterialInstanceTool::GetToolDescription() const
{
	return TEXT("Edit Material Instance Constant parameters: set scalar, vector, texture values, clear a single named override, or change the parent material.");
}

TMap<FString, FMcpSchemaProperty> UEditMaterialInstanceTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Material Instance asset path"), true));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Array of edit actions. Supported: set_scalar (parameter, value), set_vector (parameter, value[4]), set_texture (parameter, texture_path), clear_override (parameter), set_parent (parent_path).");
	ActionsSchema.bRequired = true;
	ActionsSchema.ItemsType = TEXT("object");
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));

	return Schema;
}

TArray<FString> UEditMaterialInstanceTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("actions")};
}

FMcpToolResult UEditMaterialInstanceTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array required"));
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	FString LoadError;
	UMaterialInstanceConstant* MaterialInstance = FMcpAssetModifier::LoadAssetByPath<UMaterialInstanceConstant>(AssetPath, LoadError);
	if (!MaterialInstance)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bWasModified = false;
	bool bRolledBack = false;

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Material Instance")));
		MaterialInstance->Modify();
	}

	for (int32 ActionIndex = 0; ActionIndex < ActionsArray->Num(); ++ActionIndex)
	{
		const TSharedPtr<FJsonObject>* ActionObject = nullptr;
		if (!(*ActionsArray)[ActionIndex]->TryGetObject(ActionObject) || !ActionObject || !(*ActionObject).IsValid())
		{
			TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
			ResultObject->SetNumberField(TEXT("index"), ActionIndex);
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("Action must be an object"));
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			continue;
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), ActionIndex);

		FString ActionName;
		(*ActionObject)->TryGetStringField(TEXT("action"), ActionName);
		ResultObject->SetStringField(TEXT("action"), ActionName);

		if (bDryRun)
		{
			FString ParameterNameString;
			FString ValidationError;
			const bool bValidationSuccess = EditMaterialInstanceToolPrivate::ValidateAction(*ActionObject, ActionName, ParameterNameString, ValidationError);
			ResultObject->SetStringField(TEXT("action"), ActionName);
			if (!ParameterNameString.IsEmpty())
			{
				ResultObject->SetStringField(TEXT("parameter"), ParameterNameString);
			}
			ResultObject->SetBoolField(TEXT("success"), bValidationSuccess);
			if (!bValidationSuccess)
			{
				ResultObject->SetStringField(TEXT("error"), ValidationError);
				bAnyFailed = true;
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			}
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			continue;
		}

		const bool bMarkedModified = FMcpAssetModifier::MarkModified(MaterialInstance);
		if (!bMarkedModified)
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("Material instance could not be marked modified before applying changes"))));
		}

		FString ParameterNameString;
		(*ActionObject)->TryGetStringField(TEXT("parameter"), ParameterNameString);
		const FName ParameterName(*ParameterNameString);

		bool bActionSuccess = false;
		FString ActionError;

		if (ActionName == TEXT("set_scalar"))
		{
			if (ParameterNameString.IsEmpty())
			{
				ActionError = TEXT("'parameter' is required for set_scalar");
			}
			else
			{
				double Value = 0.0;
				if (!(*ActionObject)->TryGetNumberField(TEXT("value"), Value))
				{
					ActionError = TEXT("'value' is required for set_scalar");
				}
				else
				{
					MaterialInstance->SetScalarParameterValueEditorOnly(ParameterName, static_cast<float>(Value));
					bActionSuccess = true;
					bWasModified = true;
				}
			}
		}
		else if (ActionName == TEXT("set_vector"))
		{
			if (ParameterNameString.IsEmpty())
			{
				ActionError = TEXT("'parameter' is required for set_vector");
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* ValueArray = nullptr;
				if (!(*ActionObject)->TryGetArrayField(TEXT("value"), ValueArray) || !ValueArray || ValueArray->Num() < 4)
				{
					ActionError = TEXT("'value' must be an array of 4 numbers for set_vector");
				}
				else
				{
					const FLinearColor Color(
						static_cast<float>((*ValueArray)[0]->AsNumber()),
						static_cast<float>((*ValueArray)[1]->AsNumber()),
						static_cast<float>((*ValueArray)[2]->AsNumber()),
						static_cast<float>((*ValueArray)[3]->AsNumber()));
					MaterialInstance->SetVectorParameterValueEditorOnly(ParameterName, Color);
					bActionSuccess = true;
					bWasModified = true;
				}
			}
		}
		else if (ActionName == TEXT("set_texture"))
		{
			if (ParameterNameString.IsEmpty())
			{
				ActionError = TEXT("'parameter' is required for set_texture");
			}
			else
			{
				FString TexturePath;
				(*ActionObject)->TryGetStringField(TEXT("texture_path"), TexturePath);
				if (TexturePath.IsEmpty())
				{
					ActionError = TEXT("'texture_path' is required for set_texture");
				}
				else
				{
					FString TextureError;
					UTexture* Texture = FMcpAssetModifier::LoadAssetByPath<UTexture>(TexturePath, TextureError);
					if (!Texture)
					{
						ActionError = TextureError;
					}
					else
					{
						MaterialInstance->SetTextureParameterValueEditorOnly(ParameterName, Texture);
						bActionSuccess = true;
						bWasModified = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("clear_override"))
		{
			if (ParameterNameString.IsEmpty())
			{
				ActionError = TEXT("'parameter' is required for clear_override");
			}
			else
			{
				const bool bRemoved = EditMaterialInstanceToolPrivate::RemoveNamedOverride(MaterialInstance, ParameterName);
				if (!bRemoved)
				{
					WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("No override existed for parameter '%s'"), *ParameterNameString))));
				}
				bActionSuccess = true;
				bWasModified = true;
			}
		}
		else if (ActionName == TEXT("set_parent"))
		{
			FString ParentPath;
			(*ActionObject)->TryGetStringField(TEXT("parent_path"), ParentPath);
			if (ParentPath.IsEmpty())
			{
				ActionError = TEXT("'parent_path' is required for set_parent");
			}
			else
			{
				FString ParentError;
				UMaterialInterface* ParentMaterial = FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(ParentPath, ParentError);
				if (!ParentMaterial)
				{
					ActionError = ParentError;
				}
				else
				{
					MaterialInstance->SetParentEditorOnly(ParentMaterial);
					bActionSuccess = true;
					bWasModified = true;
				}
			}
		}
		else
		{
			ActionError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		ResultObject->SetBoolField(TEXT("success"), bActionSuccess);
		if (!ParameterNameString.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("parameter"), ParameterNameString);
		}
		if (!bActionSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), ActionError);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bAnyFailed && Transaction.IsValid())
	{
		Transaction->Cancel();
		bRolledBack = true;
		bWasModified = false;
	}

	if (!bAnyFailed && bWasModified)
	{
		FMcpAssetModifier::MarkPackageDirty(MaterialInstance);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && !bAnyFailed && bSave && bWasModified)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(MaterialInstance, false, SaveError))
		{
			TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
			PartialResult->SetStringField(TEXT("tool"), TEXT("edit-material-instance"));
			PartialResult->SetBoolField(TEXT("success"), false);
			PartialResult->SetArrayField(TEXT("results"), ResultsArray);
			PartialResult->SetArrayField(TEXT("warnings"), WarningsArray);
			PartialResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
			PartialResult->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
			PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError, nullptr, PartialResult);
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), ActionsArray->Num());
	Summary->SetNumberField(TEXT("failed"), bAnyFailed ? PartialResultsArray.Num() : 0);
	Summary->SetNumberField(TEXT("modified_assets"), ModifiedAssetsArray.Num());
	Summary->SetBoolField(TEXT("saved"), !bDryRun && bSave && bWasModified && !bAnyFailed);
	Summary->SetBoolField(TEXT("rolled_back"), bRolledBack);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("edit-material-instance"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetBoolField(TEXT("rolled_back"), bRolledBack);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
