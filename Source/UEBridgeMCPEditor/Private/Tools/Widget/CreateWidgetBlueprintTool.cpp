// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Widget/CreateWidgetBlueprintTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "UEBridgeMCPEditor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace
{
static UClass* ResolveRootWidgetClass(const FString& RootWidgetClassName, FString& OutError)
{
	if (RootWidgetClassName.IsEmpty())
	{
		return UCanvasPanel::StaticClass();
	}

	FString ResolveError;
	UClass* ResolvedClass = FMcpPropertySerializer::ResolveClass(RootWidgetClassName, ResolveError);
	if (!ResolvedClass)
	{
		OutError = ResolveError;
		return nullptr;
	}

	if (!ResolvedClass->IsChildOf(UPanelWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Root widget class '%s' must inherit from UPanelWidget"), *RootWidgetClassName);
		return nullptr;
	}

	return ResolvedClass;
}

static void RemoveExistingRootWidget(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !WidgetBlueprint->WidgetTree->RootWidget)
	{
		return;
	}

	UWidget* ExistingRoot = WidgetBlueprint->WidgetTree->RootWidget;
	const FName ExistingRootName = ExistingRoot->GetFName();

	WidgetBlueprint->WidgetTree->Modify();
	WidgetBlueprint->Modify();
	ExistingRoot->Modify();

	WidgetBlueprint->WidgetTree->RootWidget = nullptr;
	ExistingRoot->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
	WidgetBlueprint->OnVariableRemoved(ExistingRootName);
}
}

FString UCreateWidgetBlueprintTool::GetToolDescription() const
{
	return TEXT("Create a Widget Blueprint asset with an optional root panel override. "
		"Supports dry_run, compile, and save options.");
}

TMap<FString, FMcpSchemaProperty> UCreateWidgetBlueprintTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Full Widget Blueprint asset path, including asset name (for example '/Game/UI/WBP_MainMenu')"),
		true));

	Schema.Add(TEXT("parent_class"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Optional UserWidget parent class path or short class name. Defaults to UserWidget.")));

	Schema.Add(TEXT("create_root_widget"), FMcpSchemaProperty::Make(
		TEXT("boolean"),
		TEXT("Whether a default root panel should exist after creation. Defaults to true.")));

	Schema.Add(TEXT("root_widget_class"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Optional root panel class when create_root_widget is true. Must inherit from PanelWidget. Defaults to CanvasPanel.")));

	Schema.Add(TEXT("root_widget_name"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Optional explicit name for the created root widget.")));

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy: 'never', 'if_needed', or 'always'"),
		{ TEXT("never"), TEXT("if_needed"), TEXT("always") }));

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(
		TEXT("boolean"),
		TEXT("Save the created Widget Blueprint after creation.")));

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(
		TEXT("boolean"),
		TEXT("Validate creation inputs without creating the asset.")));

	return Schema;
}

FMcpToolResult UCreateWidgetBlueprintTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' is required"));
	}

	const FString ParentClassName = GetStringArgOrDefault(Arguments, TEXT("parent_class"));
	const bool bCreateRootWidget = GetBoolArgOrDefault(Arguments, TEXT("create_root_widget"), true);
	const FString RootWidgetClassName = GetStringArgOrDefault(Arguments, TEXT("root_widget_class"));
	const FString RootWidgetName = GetStringArgOrDefault(Arguments, TEXT("root_widget_name"));
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("if_needed"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError, Details);
	}

	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"),
			FString::Printf(TEXT("Asset already exists: %s"), *AssetPath), Details);
	}

	UClass* ParentClass = UUserWidget::StaticClass();
	if (!ParentClassName.IsEmpty())
	{
		FString ResolveError;
		UClass* ResolvedParentClass = FMcpPropertySerializer::ResolveClass(ParentClassName, ResolveError);
		if (!ResolvedParentClass)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), ResolveError);
		}

		if (!ResolvedParentClass->IsChildOf(UUserWidget::StaticClass()))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_CLASS"),
				FString::Printf(TEXT("Parent class '%s' must inherit from UUserWidget"), *ParentClassName));
		}

		ParentClass = ResolvedParentClass;
	}

	FString RootResolveError;
	UClass* RootWidgetClass = ResolveRootWidgetClass(RootWidgetClassName, RootResolveError);
	if (!RootWidgetClass)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), RootResolveError);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetBoolField(TEXT("create_root_widget"), bCreateRootWidget);
	Response->SetStringField(TEXT("root_widget_class"), RootWidgetClass->GetPathName());
	if (!RootWidgetName.IsEmpty())
	{
		Response->SetStringField(TEXT("root_widget_name"), RootWidgetName);
	}

	if (bDryRun)
	{
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("summary"), TEXT("Validated Widget Blueprint creation request"));
		return FMcpToolResult::StructuredJson(Response);
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("UEBridgeMCP", "CreateWidgetBlueprint", "Create Widget Blueprint {0}"),
			FText::FromString(AssetPath)));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UObject* CreatedAsset = AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(CreatedAsset);
	if (!WidgetBlueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CREATE_FAILED"),
			FString::Printf(TEXT("Failed to create Widget Blueprint: %s"), *AssetPath));
	}

	if (!WidgetBlueprint->WidgetTree)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"),
			TEXT("Created Widget Blueprint has no WidgetTree"));
	}

	if (!bCreateRootWidget)
	{
		RemoveExistingRootWidget(WidgetBlueprint);
	}
	else
	{
		const bool bNeedsRootReplacement =
			WidgetBlueprint->WidgetTree->RootWidget == nullptr ||
			!WidgetBlueprint->WidgetTree->RootWidget->IsA(RootWidgetClass);

		if (bNeedsRootReplacement)
		{
			RemoveExistingRootWidget(WidgetBlueprint);

			UWidget* NewRootWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(RootWidgetClass, FName(*RootWidgetName));
			if (!NewRootWidget)
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_CREATE_FAILED"),
					FString::Printf(TEXT("Failed to construct root widget of class '%s'"), *RootWidgetClass->GetName()));
			}

			WidgetBlueprint->WidgetTree->Modify();
			WidgetBlueprint->WidgetTree->RootWidget = NewRootWidget;
			WidgetBlueprint->OnVariableAdded(NewRootWidget->GetFName());
		}
		else if (!RootWidgetName.IsEmpty())
		{
			UWidget* ExistingRoot = WidgetBlueprint->WidgetTree->RootWidget;
			if (ExistingRoot && ExistingRoot->GetFName() != FName(*RootWidgetName))
			{
				const FName OldName = ExistingRoot->GetFName();
				const FName NewName = FName(*RootWidgetName);

				WidgetBlueprint->Modify();
				ExistingRoot->Modify();
				WidgetBlueprint->OnVariableRenamed(OldName, NewName);
				ExistingRoot->SetDisplayLabel(RootWidgetName);
				ExistingRoot->Rename(*RootWidgetName, nullptr, REN_DontCreateRedirectors);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	FMcpAssetModifier::MarkPackageDirty(WidgetBlueprint);
	FAssetRegistryModule::AssetCreated(WidgetBlueprint);

	bool bCompileAttempted = false;
	bool bCompileSuccess = true;
	FString CompileError;
	if (CompilePolicy == TEXT("always") || CompilePolicy == TEXT("if_needed"))
	{
		bCompileAttempted = true;
		FMcpAssetModifier::RefreshBlueprintNodes(WidgetBlueprint);
		bCompileSuccess = FMcpAssetModifier::CompileBlueprint(WidgetBlueprint, CompileError);
	}

	bool bSaveSuccess = true;
	FString SaveError;
	if (bSave && bCompileSuccess)
	{
		bSaveSuccess = FMcpAssetModifier::SaveAsset(WidgetBlueprint, false, SaveError);
	}

	Response->SetBoolField(TEXT("success"), bCompileSuccess && bSaveSuccess);
	Response->SetStringField(TEXT("created_class"), WidgetBlueprint->GetClass()->GetName());

	if (WidgetBlueprint->WidgetTree->RootWidget)
	{
		TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
		RootObject->SetStringField(TEXT("name"), WidgetBlueprint->WidgetTree->RootWidget->GetName());
		RootObject->SetStringField(TEXT("class"), WidgetBlueprint->WidgetTree->RootWidget->GetClass()->GetName());
		Response->SetObjectField(TEXT("root_widget"), RootObject);
	}

	TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
	CompileObject->SetBoolField(TEXT("attempted"), bCompileAttempted);
	CompileObject->SetBoolField(TEXT("success"), bCompileSuccess);
	if (!CompileError.IsEmpty())
	{
		CompileObject->SetStringField(TEXT("error"), CompileError);
	}
	Response->SetObjectField(TEXT("compile"), CompileObject);

	if (bSave)
	{
		TSharedPtr<FJsonObject> SaveObject = MakeShareable(new FJsonObject);
		SaveObject->SetBoolField(TEXT("attempted"), true);
		SaveObject->SetBoolField(TEXT("success"), bSaveSuccess);
		if (!SaveError.IsEmpty())
		{
			SaveObject->SetStringField(TEXT("error"), SaveError);
		}
		Response->SetObjectField(TEXT("save"), SaveObject);
	}

	return FMcpToolResult::StructuredJson(Response, !(bCompileSuccess && bSaveSuccess));
}
