// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/CreateMaterialInstanceTool.h"

#include "Tools/Material/EditMaterialInstanceTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace
{
	void AppendNamedArrayField(const TSharedPtr<FJsonObject>& Source, const FString& FieldName, TArray<TSharedPtr<FJsonValue>>& OutTarget)
	{
		const TArray<TSharedPtr<FJsonValue>>* SourceArray = nullptr;
		if (Source.IsValid() && Source->TryGetArrayField(FieldName, SourceArray) && SourceArray)
		{
			OutTarget.Append(*SourceArray);
		}
	}
}

FString UCreateMaterialInstanceTool::GetToolDescription() const
{
	return TEXT("Create a Material Instance Constant from a parent material, with optional initial parameter actions that reuse edit-material-instance semantics.");
}

TMap<FString, FMcpSchemaProperty> UCreateMaterialInstanceTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("New material instance asset path"), true));
	Schema.Add(TEXT("parent_material_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Parent material or material instance asset path"), true));

	FMcpSchemaProperty InitialActionsSchema;
	InitialActionsSchema.Type = TEXT("array");
	InitialActionsSchema.Description = TEXT("Optional initial parameter actions using edit-material-instance action schema");
	InitialActionsSchema.ItemsType = TEXT("object");
	Schema.Add(TEXT("initial_actions"), InitialActionsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the new material instance asset")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));

	return Schema;
}

TArray<FString> UCreateMaterialInstanceTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("parent_material_path") };
}

FMcpToolResult UCreateMaterialInstanceTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString ParentMaterialPath = GetStringArgOrDefault(Arguments, TEXT("parent_material_path"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	if (AssetPath.IsEmpty() || ParentMaterialPath.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' and 'parent_material_path' are required"));
	}

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ValidateError);
	}

	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Asset already exists"), Details);
	}

	FString LoadError;
	UMaterialInterface* ParentMaterial = FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(ParentMaterialPath, LoadError);
	if (!ParentMaterial)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	const TArray<TSharedPtr<FJsonValue>>* InitialActions = nullptr;
	Arguments->TryGetArrayField(TEXT("initial_actions"), InitialActions);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetStringField(TEXT("asset_path"), AssetPath);
		ResultObject->SetStringField(TEXT("parent_material_path"), ParentMaterialPath);
		ResultObject->SetBoolField(TEXT("would_create"), true);
		ResultObject->SetNumberField(TEXT("initial_action_count"), InitialActions ? InitialActions->Num() : 0);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), GetToolName());
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("asset_path"), AssetPath);
		Response->SetStringField(TEXT("parent_material_path"), ParentMaterialPath);
		Response->SetArrayField(TEXT("results"), ResultsArray);
		Response->SetArrayField(TEXT("warnings"), WarningsArray);
		Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return FMcpToolResult::StructuredJson(Response);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Material Instance")));

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMaterial;

	UObject* CreatedObject = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(CreatedObject);
	if (!MaterialInstance)
	{
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create material instance asset"));
	}

	FAssetRegistryModule::AssetCreated(MaterialInstance);
	FMcpAssetModifier::MarkPackageDirty(MaterialInstance);
	ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));

	TSharedPtr<FJsonObject> CreateResult = MakeShareable(new FJsonObject);
	CreateResult->SetStringField(TEXT("asset_path"), AssetPath);
	CreateResult->SetStringField(TEXT("created_class"), MaterialInstance->GetClass()->GetName());
	CreateResult->SetStringField(TEXT("parent_material_path"), ParentMaterialPath);
	CreateResult->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, MaterialInstance->GetClass()->GetName()));
	ResultsArray.Add(MakeShareable(new FJsonValueObject(CreateResult)));

	if (InitialActions && InitialActions->Num() > 0)
	{
		TSharedPtr<FJsonObject> InitialEditArguments = MakeShareable(new FJsonObject);
		InitialEditArguments->SetStringField(TEXT("asset_path"), AssetPath);
		InitialEditArguments->SetArrayField(TEXT("actions"), *InitialActions);
		InitialEditArguments->SetBoolField(TEXT("save"), false);
		InitialEditArguments->SetBoolField(TEXT("dry_run"), false);

		FMcpToolResult InitialEditResult = GetMutableDefault<UEditMaterialInstanceTool>()->Execute(InitialEditArguments, Context);
		const TSharedPtr<FJsonObject> StructuredContent = InitialEditResult.GetStructuredContent();
		if (InitialEditResult.bIsError || !InitialEditResult.bSuccess)
		{
			if (StructuredContent.IsValid())
			{
				AppendNamedArrayField(StructuredContent, TEXT("results"), ResultsArray);
				AppendNamedArrayField(StructuredContent, TEXT("warnings"), WarningsArray);
				AppendNamedArrayField(StructuredContent, TEXT("diagnostics"), DiagnosticsArray);
				AppendNamedArrayField(StructuredContent, TEXT("partial_results"), PartialResultsArray);
			}
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(
				TEXT("UEBMCP_OPERATION_FAILED"),
				TEXT("Material instance was created but initial_actions failed"),
				nullptr,
				StructuredContent);
		}

		AppendNamedArrayField(StructuredContent, TEXT("results"), ResultsArray);
		AppendNamedArrayField(StructuredContent, TEXT("warnings"), WarningsArray);
		AppendNamedArrayField(StructuredContent, TEXT("diagnostics"), DiagnosticsArray);
		AppendNamedArrayField(StructuredContent, TEXT("partial_results"), PartialResultsArray);
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(MaterialInstance);
	FMcpAssetModifier::MarkPackageDirty(MaterialInstance);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(MaterialInstance, false, SaveError))
		{
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("parent_material_path"), ParentMaterialPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response);
}
