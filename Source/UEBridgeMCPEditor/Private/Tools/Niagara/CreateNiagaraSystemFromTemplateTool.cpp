// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Niagara/CreateNiagaraSystemFromTemplateTool.h"

#include "Tools/Niagara/NiagaraToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ScopedTransaction.h"

FString UCreateNiagaraSystemFromTemplateTool::GetToolDescription() const
{
	return TEXT("Create a Niagara system from an optional template system. Without a template, creates a new empty/default Niagara system via Niagara's editor factory.");
}

TMap<FString, FMcpSchemaProperty> UCreateNiagaraSystemFromTemplateTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination Niagara system asset path"), true));
	Schema.Add(TEXT("template_system_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional source Niagara system to duplicate")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Request a Niagara compile after creation")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the created system asset")));
	return Schema;
}

FMcpToolResult UCreateNiagaraSystemFromTemplateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString TemplateSystemPath = GetStringArgOrDefault(Arguments, TEXT("template_system_path"));
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}

	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Destination asset already exists"), Details);
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Niagara System")));
	UNiagaraSystem* CreatedSystem = nullptr;

	if (!TemplateSystemPath.IsEmpty())
	{
		FString LoadError;
		UNiagaraSystem* TemplateSystem = FMcpAssetModifier::LoadAssetByPath<UNiagaraSystem>(TemplateSystemPath, LoadError);
		if (!TemplateSystem)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PACKAGE_CREATE_FAILED"), TEXT("Failed to create destination package"));
		}

		CreatedSystem = Cast<UNiagaraSystem>(StaticDuplicateObject(
			TemplateSystem,
			Package,
			FName(*AssetName),
			RF_Public | RF_Standalone | RF_Transactional));
	}
	else
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
		UObject* CreatedObject = AssetTools.CreateAsset(AssetName, PackagePath, UNiagaraSystem::StaticClass(), Factory);
		CreatedSystem = Cast<UNiagaraSystem>(CreatedObject);
	}

	if (!CreatedSystem)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_NIAGARA_CREATE_FAILED"), TEXT("Failed to create Niagara system"));
	}

	FMcpAssetModifier::MarkPackageDirty(CreatedSystem);
	FAssetRegistryModule::AssetCreated(CreatedSystem);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
	CompileObject->SetBoolField(TEXT("requested"), false);
	if (bCompile)
	{
		CompileObject = NiagaraToolUtils::CompileSystem(CreatedSystem, true);
	}

	if (bSave)
	{
		NiagaraToolUtils::SaveAsset(CreatedSystem, Warnings);
	}

	TSharedPtr<FJsonObject> Result = NiagaraToolUtils::SerializeSystemSummary(CreatedSystem, true, true);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("creation_mode"), TemplateSystemPath.IsEmpty() ? TEXT("new_empty_system") : TEXT("template_duplicate"));
	if (!TemplateSystemPath.IsEmpty())
	{
		Result->SetStringField(TEXT("template_system_path"), TemplateSystemPath);
	}
	Result->SetObjectField(TEXT("compile"), CompileObject);
	Result->SetBoolField(TEXT("saved"), bSave && Warnings.Num() == 0);
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetBoolField(TEXT("needs_save"), CreatedSystem->GetOutermost()->IsDirty());

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Niagara system created"));
}
