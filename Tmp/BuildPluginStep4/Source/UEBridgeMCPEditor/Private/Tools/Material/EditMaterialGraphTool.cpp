// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/EditMaterialGraphTool.h"

#include "Tools/Material/MaterialToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Misc/PackageName.h"

namespace
{
	enum class EMaterialCompileMode : uint8
	{
		Never,
		IfNeeded,
		Always
	};

	void AppendNamedArrayField(const TSharedPtr<FJsonObject>& Source, const FString& FieldName, TArray<TSharedPtr<FJsonValue>>& OutTarget)
	{
		const TArray<TSharedPtr<FJsonValue>>* SourceArray = nullptr;
		if (Source.IsValid() && Source->TryGetArrayField(FieldName, SourceArray) && SourceArray)
		{
			OutTarget.Append(*SourceArray);
		}
	}

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

	bool TryGetOperationsArray(
		const TSharedPtr<FJsonObject>& Arguments,
		const TArray<TSharedPtr<FJsonValue>>*& OutOperations,
		FString& OutError)
	{
		if (!Arguments->TryGetArrayField(TEXT("operations"), OutOperations) || !OutOperations || OutOperations->Num() == 0)
		{
			OutError = TEXT("'operations' array is required");
			return false;
		}
		return true;
	}

	bool TryParseCompileMode(const FString& Value, EMaterialCompileMode& OutMode)
	{
		if (Value.Equals(TEXT("never"), ESearchCase::IgnoreCase))
		{
			OutMode = EMaterialCompileMode::Never;
			return true;
		}
		if (Value.Equals(TEXT("if_needed"), ESearchCase::IgnoreCase))
		{
			OutMode = EMaterialCompileMode::IfNeeded;
			return true;
		}
		if (Value.Equals(TEXT("always"), ESearchCase::IgnoreCase))
		{
			OutMode = EMaterialCompileMode::Always;
			return true;
		}
		return false;
	}

	UMaterialExpression* ResolveExpressionReference(
		UMaterial* Material,
		const TMap<FString, UMaterialExpression*>& AliasMap,
		const TSharedPtr<FJsonObject>& Operation,
		const TCHAR* ExpressionField,
		const TCHAR* AliasField,
		FString& OutReferenceLabel,
		FString& OutError)
	{
		FString ExpressionName;
		Operation->TryGetStringField(ExpressionField, ExpressionName);
		FString AliasName;
		Operation->TryGetStringField(AliasField, AliasName);

		if (ExpressionName.IsEmpty() && AliasName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' or '%s' is required"), ExpressionField, AliasField);
			return nullptr;
		}

		if (!AliasName.IsEmpty())
		{
			if (UMaterialExpression* const* AliasExpression = AliasMap.Find(AliasName))
			{
				OutReferenceLabel = AliasName;
				return *AliasExpression;
			}
		}

		if (!ExpressionName.IsEmpty())
		{
			if (UMaterialExpression* FoundExpression = MaterialToolUtils::FindExpressionByName(Material, ExpressionName))
			{
				OutReferenceLabel = ExpressionName;
				return FoundExpression;
			}
		}

		OutError = !AliasName.IsEmpty()
			? FString::Printf(TEXT("Expression alias '%s' was not found"), *AliasName)
			: FString::Printf(TEXT("Expression '%s' was not found"), *ExpressionName);
		return nullptr;
	}

	void DisconnectExpressionInput(FExpressionInput* Input)
	{
		if (!Input)
		{
			return;
		}

		Input->Expression = nullptr;
		Input->OutputIndex = 0;
		Input->SetMask(0, 0, 0, 0, 0);
	}
}

FString UEditMaterialGraphTool::GetToolDescription() const
{
	return TEXT("Transactional material graph editing for UMaterial assets. Supports expression creation, removal, property edits, connection updates, layout, compile control, and dry-run validation.");
}

TMap<FString, FMcpSchemaProperty> UEditMaterialGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("UMaterial asset path"), true));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Compile mode"), { TEXT("never"), TEXT("if_needed"), TEXT("always") }));

	TSharedPtr<FJsonObject> PropertiesRawSchema = MakeShareable(new FJsonObject);
	PropertiesRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	PropertiesRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> PropertiesSchema = MakeShared<FMcpSchemaProperty>();
	PropertiesSchema->Description = TEXT("Expression property overrides");
	PropertiesSchema->RawSchema = PropertiesRawSchema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Material graph operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Material graph action"),
		{
			TEXT("add_expression"),
			TEXT("remove_expression"),
			TEXT("set_expression_properties"),
			TEXT("connect_expressions"),
			TEXT("disconnect_input"),
			TEXT("connect_property"),
			TEXT("disconnect_property"),
			TEXT("layout_expressions")
		},
		true)));
	OperationSchema->Properties.Add(TEXT("alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transient alias for add_expression within this request"))));
	OperationSchema->Properties.Add(TEXT("expression_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Material expression class for add_expression"))));
	OperationSchema->Properties.Add(TEXT("expression_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing expression name"))));
	OperationSchema->Properties.Add(TEXT("from_expression"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source expression name"))));
	OperationSchema->Properties.Add(TEXT("from_alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source alias from add_expression"))));
	OperationSchema->Properties.Add(TEXT("to_expression"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target expression name"))));
	OperationSchema->Properties.Add(TEXT("to_alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target alias from add_expression"))));
	OperationSchema->Properties.Add(TEXT("from_output"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source output name or output index string"))));
	OperationSchema->Properties.Add(TEXT("to_input"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target input name"))));
	OperationSchema->Properties.Add(TEXT("material_property"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Material property name like BaseColor or EmissiveColor"))));
	OperationSchema->Properties.Add(TEXT("properties"), PropertiesSchema);
	OperationSchema->Properties.Add(TEXT("position"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Expression node position [x, y]"), TEXT("number"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Material graph operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save material after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));

	return Schema;
}

TArray<FString> UEditMaterialGraphTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("operations") };
}

FMcpToolResult UEditMaterialGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	EMaterialCompileMode CompileMode = EMaterialCompileMode::IfNeeded;
	const FString CompileModeString = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("if_needed"));
	if (!TryParseCompileMode(CompileModeString, CompileMode))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'compile' must be 'never', 'if_needed', or 'always'"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	FString OperationArrayError;
	if (!TryGetOperationsArray(Arguments, Operations, OperationArrayError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), OperationArrayError);
	}

	FString LoadError;
	UMaterial* Material = FMcpAssetModifier::LoadAssetByPath<UMaterial>(AssetPath, LoadError);
	if (!Material)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Material Graph")));
		FMcpAssetModifier::MarkModified(Material);
	}

	TMap<FString, UMaterialExpression*> AliasMap;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bMaterialChanged = false;
	bool bCompiled = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
			}
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

		if (ActionName == TEXT("add_expression"))
		{
			FString ExpressionClassName;
			if (!(*OperationObject)->TryGetStringField(TEXT("expression_class"), ExpressionClassName))
			{
				OperationError = TEXT("'expression_class' is required for add_expression");
			}
			else
			{
				UClass* ExpressionClass = nullptr;
				if (!MaterialToolUtils::ResolveExpressionClass(ExpressionClassName, ExpressionClass, OperationError))
				{
				}
				else
				{
					const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
					int32 NodePosX = 0;
					int32 NodePosY = 0;
					if ((*OperationObject)->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray && PositionArray->Num() >= 2)
					{
						NodePosX = static_cast<int32>((*PositionArray)[0]->AsNumber());
						NodePosY = static_cast<int32>((*PositionArray)[1]->AsNumber());
					}

					const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
					(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject);

					UMaterialExpression* Expression = nullptr;
					if (bDryRun)
					{
						Expression = NewObject<UMaterialExpression>(GetTransientPackage(), ExpressionClass);
						if (!Expression)
						{
							OperationError = TEXT("Failed to create transient expression for validation");
						}
						else
						{
							Expression->MaterialExpressionEditorX = NodePosX;
							Expression->MaterialExpressionEditorY = NodePosY;
							if (PropertiesObject && (*PropertiesObject).IsValid() && !MaterialToolUtils::ApplyObjectProperties(Expression, *PropertiesObject, OperationError))
							{
								Expression = nullptr;
							}
						}
					}
					else
					{
						Expression = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionClass, NodePosX, NodePosY);
						if (!Expression)
						{
							OperationError = TEXT("Failed to create material expression");
						}
						else
						{
							Expression->Modify();
							if (PropertiesObject && (*PropertiesObject).IsValid() && !MaterialToolUtils::ApplyObjectProperties(Expression, *PropertiesObject, OperationError))
							{
								UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
								Expression = nullptr;
							}
						}
					}

					if (Expression)
					{
						FString AliasName;
						(*OperationObject)->TryGetStringField(TEXT("alias"), AliasName);
						if (!AliasName.IsEmpty())
						{
							AliasMap.Add(AliasName, Expression);
							ResultObject->SetStringField(TEXT("alias"), AliasName);
						}
						ResultObject->SetStringField(TEXT("expression_name"), Expression->GetName());
						ResultObject->SetStringField(TEXT("expression_class"), Expression->GetClass()->GetName());
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("remove_expression"))
		{
			FString ReferenceLabel;
			UMaterialExpression* Expression = ResolveExpressionReference(Material, AliasMap, *OperationObject, TEXT("expression_name"), TEXT("alias"), ReferenceLabel, OperationError);
			if (Expression)
			{
				ResultObject->SetStringField(TEXT("expression"), ReferenceLabel);
				if (!bDryRun)
				{
					UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
					for (auto It = AliasMap.CreateIterator(); It; ++It)
					{
						if (It.Value() == Expression)
						{
							It.RemoveCurrent();
						}
					}
				}
				bOperationSuccess = true;
				bOperationChanged = true;
			}
		}
		else if (ActionName == TEXT("set_expression_properties"))
		{
			FString ReferenceLabel;
			UMaterialExpression* Expression = ResolveExpressionReference(Material, AliasMap, *OperationObject, TEXT("expression_name"), TEXT("alias"), ReferenceLabel, OperationError);
			const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
			if (Expression && (!(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !(*PropertiesObject).IsValid()))
			{
				OperationError = TEXT("'properties' object is required for set_expression_properties");
				Expression = nullptr;
			}

			if (Expression)
			{
				if (!bDryRun)
				{
					Expression->Modify();
				}
				if (MaterialToolUtils::ApplyObjectProperties(Expression, *PropertiesObject, OperationError))
				{
					ResultObject->SetStringField(TEXT("expression"), ReferenceLabel);
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
		}
		else if (ActionName == TEXT("connect_expressions"))
		{
			FString FromReferenceLabel;
			FString ToReferenceLabel;
			UMaterialExpression* FromExpression = ResolveExpressionReference(Material, AliasMap, *OperationObject, TEXT("from_expression"), TEXT("from_alias"), FromReferenceLabel, OperationError);
			UMaterialExpression* ToExpression = FromExpression
				? ResolveExpressionReference(Material, AliasMap, *OperationObject, TEXT("to_expression"), TEXT("to_alias"), ToReferenceLabel, OperationError)
				: nullptr;

			FString FromOutputName;
			int32 FromOutputIndex = 0;
			FString RequestedOutput;
			(*OperationObject)->TryGetStringField(TEXT("from_output"), RequestedOutput);
			if (ToExpression && !MaterialToolUtils::ResolveExpressionOutputName(FromExpression, RequestedOutput, FromOutputName, FromOutputIndex, OperationError))
			{
				ToExpression = nullptr;
			}

			FExpressionInput* TargetInput = nullptr;
			FString TargetInputName;
			(*OperationObject)->TryGetStringField(TEXT("to_input"), TargetInputName);
			if (ToExpression && !MaterialToolUtils::ResolveExpressionInput(ToExpression, TargetInputName, TargetInput, OperationError))
			{
				ToExpression = nullptr;
			}

			if (ToExpression)
			{
				ResultObject->SetStringField(TEXT("from_expression"), FromReferenceLabel);
				ResultObject->SetStringField(TEXT("to_expression"), ToReferenceLabel);
				ResultObject->SetStringField(TEXT("to_input"), TargetInputName);
				ResultObject->SetStringField(TEXT("from_output"), FromOutputName);
				if (!bDryRun)
				{
					if (!UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutputName, ToExpression, TargetInputName))
					{
						OperationError = TEXT("Failed to connect expressions");
					}
				}
				if (OperationError.IsEmpty())
				{
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
		}
		else if (ActionName == TEXT("disconnect_input"))
		{
			FString ToReferenceLabel;
			UMaterialExpression* ToExpression = ResolveExpressionReference(Material, AliasMap, *OperationObject, TEXT("to_expression"), TEXT("to_alias"), ToReferenceLabel, OperationError);
			FExpressionInput* TargetInput = nullptr;
			FString TargetInputName;
			(*OperationObject)->TryGetStringField(TEXT("to_input"), TargetInputName);
			if (ToExpression && !MaterialToolUtils::ResolveExpressionInput(ToExpression, TargetInputName, TargetInput, OperationError))
			{
				ToExpression = nullptr;
			}

			if (ToExpression)
			{
				ResultObject->SetStringField(TEXT("to_expression"), ToReferenceLabel);
				ResultObject->SetStringField(TEXT("to_input"), TargetInputName);
				bOperationChanged = TargetInput->Expression != nullptr;
				if (!bDryRun)
				{
					ToExpression->Modify();
					DisconnectExpressionInput(TargetInput);
				}
				bOperationSuccess = true;
			}
		}
		else if (ActionName == TEXT("connect_property"))
		{
			FString FromReferenceLabel;
			UMaterialExpression* FromExpression = ResolveExpressionReference(Material, AliasMap, *OperationObject, TEXT("from_expression"), TEXT("from_alias"), FromReferenceLabel, OperationError);

			FString RequestedOutput;
			(*OperationObject)->TryGetStringField(TEXT("from_output"), RequestedOutput);
			FString FromOutputName;
			int32 FromOutputIndex = 0;
			if (FromExpression && !MaterialToolUtils::ResolveExpressionOutputName(FromExpression, RequestedOutput, FromOutputName, FromOutputIndex, OperationError))
			{
				FromExpression = nullptr;
			}

			FString PropertyName;
			(*OperationObject)->TryGetStringField(TEXT("material_property"), PropertyName);
			EMaterialProperty MaterialProperty = MP_MAX;
			if (FromExpression && !MaterialToolUtils::TryParseMaterialProperty(PropertyName, MaterialProperty))
			{
				OperationError = FString::Printf(TEXT("Unknown material_property '%s'"), *PropertyName);
				FromExpression = nullptr;
			}

			if (FromExpression)
			{
				ResultObject->SetStringField(TEXT("from_expression"), FromReferenceLabel);
				ResultObject->SetStringField(TEXT("material_property"), MaterialToolUtils::MaterialPropertyToString(MaterialProperty));
				ResultObject->SetStringField(TEXT("from_output"), FromOutputName);
				if (!bDryRun)
				{
					if (!UMaterialEditingLibrary::ConnectMaterialProperty(FromExpression, FromOutputName, MaterialProperty))
					{
						OperationError = TEXT("Failed to connect expression to material property");
					}
				}
				if (OperationError.IsEmpty())
				{
					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
		}
		else if (ActionName == TEXT("disconnect_property"))
		{
			FString PropertyName;
			(*OperationObject)->TryGetStringField(TEXT("material_property"), PropertyName);
			EMaterialProperty MaterialProperty = MP_MAX;
			if (!MaterialToolUtils::TryParseMaterialProperty(PropertyName, MaterialProperty))
			{
				OperationError = FString::Printf(TEXT("Unknown material_property '%s'"), *PropertyName);
			}
			else
			{
				FExpressionInput* PropertyInput = Material->GetExpressionInputForProperty(MaterialProperty);
				if (!PropertyInput)
				{
					OperationError = TEXT("Material property is not editable on this material");
				}
				else
				{
					ResultObject->SetStringField(TEXT("material_property"), MaterialToolUtils::MaterialPropertyToString(MaterialProperty));
					bOperationChanged = PropertyInput->Expression != nullptr;
					if (!bDryRun)
					{
						DisconnectExpressionInput(PropertyInput);
					}
					bOperationSuccess = true;
				}
			}
		}
		else if (ActionName == TEXT("layout_expressions"))
		{
			if (!bDryRun)
			{
				UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
			}
			bOperationSuccess = true;
			bOperationChanged = Material->GetExpressions().Num() > 0;
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
			bMaterialChanged = bMaterialChanged || bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bMaterialChanged)
	{
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		FMcpAssetModifier::MarkPackageDirty(Material);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
	}

	if (!bDryRun && (CompileMode == EMaterialCompileMode::Always || (CompileMode == EMaterialCompileMode::IfNeeded && bMaterialChanged)))
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		bCompiled = true;
	}

	if (!bDryRun && bSave && bMaterialChanged)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Material, false, SaveError))
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
	Response->SetBoolField(TEXT("compiled"), bCompiled);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
