// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Widget/CommonUITools.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace
{
	FString CommonUIClassPathForType(const FString& Type)
	{
		if (Type.Equals(TEXT("CommonActivatableWidget"), ESearchCase::IgnoreCase))
		{
			return TEXT("/Script/CommonUI.CommonActivatableWidget");
		}
		if (Type.Equals(TEXT("CommonUserWidget"), ESearchCase::IgnoreCase))
		{
			return TEXT("/Script/CommonUI.CommonUserWidget");
		}
		if (Type.Equals(TEXT("CommonButtonBase"), ESearchCase::IgnoreCase))
		{
			return TEXT("/Script/CommonUI.CommonButtonBase");
		}
		return Type;
	}

	UClass* LoadWidgetParentClass(const FString& CommonUIType, FString& OutError)
	{
		const FString ClassPath = CommonUIClassPathForType(CommonUIType);
		UClass* ParentClass = LoadClass<UObject>(nullptr, *ClassPath);
		if (!ParentClass)
		{
			OutError = FString::Printf(TEXT("CommonUI class could not be loaded: %s. Enable the CommonUI plugin or provide a valid class path."), *ClassPath);
			return nullptr;
		}
		if (!ParentClass->IsChildOf(UUserWidget::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Class '%s' must inherit from UUserWidget"), *ClassPath);
			return nullptr;
		}
		return ParentClass;
	}

	UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || WidgetName.IsEmpty())
		{
			return nullptr;
		}
		return WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	}

	bool ApplyPropertyObject(UObject* TargetObject, const TSharedPtr<FJsonObject>& PropertiesObject, bool bApply, FString& OutError)
	{
		if (!TargetObject)
		{
			OutError = TEXT("Target object is null");
			return false;
		}
		if (!PropertiesObject.IsValid())
		{
			OutError = TEXT("'properties' must be an object");
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
		{
			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString PropertyError;
			if (!FMcpAssetModifier::FindPropertyByPath(TargetObject, Pair.Key, Property, Container, PropertyError))
			{
				OutError = FString::Printf(TEXT("Property '%s' not found: %s"), *Pair.Key, *PropertyError);
				return false;
			}
			if (bApply && !FMcpAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, PropertyError))
			{
				OutError = FString::Printf(TEXT("Failed to set property '%s': %s"), *Pair.Key, *PropertyError);
				return false;
			}
		}
		return true;
	}

	TSharedPtr<FJsonObject> SerializeWidget(UWidget* Widget)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Widget)
		{
			return Object;
		}
		Object->SetStringField(TEXT("name"), Widget->GetName());
		Object->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());
		Object->SetBoolField(TEXT("is_common_ui"), Widget->GetClass()->GetPathName().Contains(TEXT("/Script/CommonUI.")));
		return Object;
	}
}

FString UCreateCommonUIWidgetTool::GetToolDescription() const
{
	return TEXT("Create a Widget Blueprint whose parent class is a CommonUI widget class, using dynamic class loading so CommonUI remains optional.");
}

TMap<FString, FMcpSchemaProperty> UCreateCommonUIWidgetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Full asset path for the new Widget Blueprint"), true));
	Schema.Add(TEXT("commonui_type"), FMcpSchemaProperty::MakeEnum(TEXT("CommonUI parent type or a full class path"), { TEXT("CommonActivatableWidget"), TEXT("CommonUserWidget"), TEXT("CommonButtonBase") }, true));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after creation. Default: true.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without creating the asset.")));
	return Schema;
}

FMcpToolResult UCreateCommonUIWidgetTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString CommonUIType = GetStringArgOrDefault(Arguments, TEXT("commonui_type"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	if (AssetPath.IsEmpty() || CommonUIType.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' and 'commonui_type' are required"));
	}

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	FString ClassError;
	UClass* ParentClass = LoadWidgetParentClass(CommonUIType, ClassError);
	if (!ParentClass)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMMONUI_UNAVAILABLE"), ClassError);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());

	if (bDryRun)
	{
		return FMcpToolResult::StructuredJson(Response);
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create CommonUI Widget Blueprint")));
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	UObject* Asset = AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Asset);
	if (!WidgetBlueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CREATE_FAILED"), FString::Printf(TEXT("Failed to create CommonUI widget: %s"), *AssetPath));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	FMcpAssetModifier::MarkPackageDirty(WidgetBlueprint);
	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(WidgetBlueprint, false, SaveError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}
	return FMcpToolResult::StructuredJson(Response);
}

FString UEditCommonUITool::GetToolDescription() const
{
	return TEXT("Batch-edit CommonUI-related widget properties on a Widget Blueprint, including button properties via generic reflection.");
}

TMap<FString, FMcpSchemaProperty> UEditCommonUITool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Widget Blueprint asset path"), true));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("Operations: set_widget_properties or set_class_defaults"), TEXT("object"), true));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after edit. Default: true.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without editing.")));
	return Schema;
}

FMcpToolResult UEditCommonUITool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UWidgetBlueprint* WidgetBlueprint = FMcpAssetModifier::LoadAssetByPath<UWidgetBlueprint>(AssetPath, LoadError);
	if (!WidgetBlueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit CommonUI Widget")));
		FMcpAssetModifier::MarkModified(WidgetBlueprint);
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	bool bAnyFailed = false;
	bool bAnyChanged = false;
	for (int32 Index = 0; Index < Operations->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Operation = nullptr;
		if (!(*Operations)[Index]->TryGetObject(Operation) || !Operation || !Operation->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), Index));
		}

		FString Action;
		(*Operation)->TryGetStringField(TEXT("action"), Action);
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("action"), Action);

		bool bSuccess = false;
		FString Error;
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		(*Operation)->TryGetObjectField(TEXT("properties"), Properties);

		if (Action == TEXT("set_widget_properties"))
		{
			FString WidgetName;
			(*Operation)->TryGetStringField(TEXT("widget_name"), WidgetName);
			UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
			if (!Widget)
			{
				Error = FString::Printf(TEXT("Widget not found: %s"), *WidgetName);
			}
			else
			{
				bSuccess = ApplyPropertyObject(Widget, Properties ? *Properties : nullptr, !bDryRun, Error);
				Result->SetObjectField(TEXT("widget"), SerializeWidget(Widget));
			}
		}
		else if (Action == TEXT("set_class_defaults"))
		{
			UObject* CDO = WidgetBlueprint->GeneratedClass ? WidgetBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
			bSuccess = ApplyPropertyObject(CDO, Properties ? *Properties : nullptr, !bDryRun, Error);
		}
		else
		{
			Error = FString::Printf(TEXT("Unsupported action: %s"), *Action);
		}

		Result->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			bAnyFailed = true;
			Result->SetStringField(TEXT("error"), Error);
		}
		else
		{
			bAnyChanged = true;
		}
		Results.Add(MakeShareable(new FJsonValueObject(Result)));
	}

	if (!bDryRun && bAnyChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
		FMcpAssetModifier::MarkPackageDirty(WidgetBlueprint);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(WidgetBlueprint, false, SaveError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), Results);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

FString UQueryCommonUIWidgetsTool::GetToolDescription() const
{
	return TEXT("List Widget Blueprints whose generated parent class or widget tree references CommonUI classes.");
}

TMap<FString, FMcpSchemaProperty> UQueryCommonUIWidgetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Content path to scan. Default: /Game")));
	Schema.Add(TEXT("name_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional substring/wildcard filter")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum results. Default: 100")));
	return Schema;
}

FMcpToolResult UQueryCommonUIWidgetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Path = GetStringArgOrDefault(Arguments, TEXT("path"), TEXT("/Game"));
	const FString NameFilter = GetStringArgOrDefault(Arguments, TEXT("name_filter"));
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 100), 1, 1000);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), Assets, true);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		if (Results.Num() >= Limit)
		{
			break;
		}
		const FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (!ClassName.Contains(TEXT("WidgetBlueprint")))
		{
			continue;
		}
		if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().MatchesWildcard(NameFilter, ESearchCase::IgnoreCase) && !Asset.AssetName.ToString().Contains(NameFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Asset.GetAsset());
		if (!WidgetBlueprint)
		{
			continue;
		}

		const FString ParentPath = WidgetBlueprint->GeneratedClass && WidgetBlueprint->GeneratedClass->GetSuperClass()
			? WidgetBlueprint->GeneratedClass->GetSuperClass()->GetPathName()
			: FString();
		bool bLooksCommonUI = ParentPath.Contains(TEXT("/Script/CommonUI."));
		if (!bLooksCommonUI && WidgetBlueprint->WidgetTree)
		{
			TArray<UWidget*> Widgets;
			WidgetBlueprint->WidgetTree->GetAllWidgets(Widgets);
			for (UWidget* Widget : Widgets)
			{
				if (Widget && Widget->GetClass()->GetPathName().Contains(TEXT("/Script/CommonUI.")))
				{
					bLooksCommonUI = true;
					break;
				}
			}
		}

		if (!bLooksCommonUI)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
		Object->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
		Object->SetStringField(TEXT("parent_class"), ParentPath);
		Results.Add(MakeShareable(new FJsonValueObject(Object)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("path"), Path);
	Response->SetNumberField(TEXT("result_count"), Results.Num());
	Response->SetArrayField(TEXT("widgets"), Results);
	return FMcpToolResult::StructuredJson(Response);
}
