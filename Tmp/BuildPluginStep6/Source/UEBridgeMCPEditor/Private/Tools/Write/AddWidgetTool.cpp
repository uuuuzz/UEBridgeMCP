// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Write/AddWidgetTool.h"
#include "Utils/McpAssetModifier.h"
#include "UEBridgeMCPEditor.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "ScopedTransaction.h"

FString UAddWidgetTool::GetToolDescription() const
{
	return TEXT("Add a widget to a WidgetBlueprint tree.");
}

TMap<FString, FMcpSchemaProperty> UAddWidgetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("WidgetBlueprint asset path");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FMcpSchemaProperty WidgetClass;
	WidgetClass.Type = TEXT("string");
	WidgetClass.Description = TEXT("Widget class: 'Button', 'TextBlock', 'Image', 'ProgressBar', 'CanvasPanel', 'VerticalBox', 'HorizontalBox', 'Border', 'Overlay'");
	WidgetClass.bRequired = true;
	Schema.Add(TEXT("widget_class"), WidgetClass);

	FMcpSchemaProperty WidgetName;
	WidgetName.Type = TEXT("string");
	WidgetName.Description = TEXT("Name for the new widget");
	WidgetName.bRequired = true;
	Schema.Add(TEXT("widget_name"), WidgetName);

	FMcpSchemaProperty ParentWidget;
	ParentWidget.Type = TEXT("string");
	ParentWidget.Description = TEXT("Parent widget name. If empty, adds to root.");
	ParentWidget.bRequired = false;
	Schema.Add(TEXT("parent_widget"), ParentWidget);

	return Schema;
}

TArray<FString> UAddWidgetTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("widget_class"), TEXT("widget_name") };
}

FMcpToolResult UAddWidgetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString WidgetClass = GetStringArgOrDefault(Arguments, TEXT("widget_class"));
	FString WidgetName = GetStringArgOrDefault(Arguments, TEXT("widget_name"));
	FString ParentWidget = GetStringArgOrDefault(Arguments, TEXT("parent_widget"));

	if (AssetPath.IsEmpty() || WidgetClass.IsEmpty() || WidgetName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("asset_path, widget_class, and widget_name are required"));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("add-widget: %s (%s) to %s"), *WidgetName, *WidgetClass, *AssetPath);

	// Load the WidgetBlueprint
	FString LoadError;
	UWidgetBlueprint* WidgetBP = FMcpAssetModifier::LoadAssetByPath<UWidgetBlueprint>(AssetPath, LoadError);
	if (!WidgetBP)
	{
		return FMcpToolResult::Error(LoadError);
	}

	UWidgetTree* WidgetTree = WidgetBP->WidgetTree;
	if (!WidgetTree)
	{
		return FMcpToolResult::Error(TEXT("WidgetBlueprint has no WidgetTree"));
	}

	// Find widget class
	UClass* WClass = nullptr;

	if (WidgetClass.Equals(TEXT("Button"), ESearchCase::IgnoreCase))
	{
		WClass = UButton::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("TextBlock"), ESearchCase::IgnoreCase))
	{
		WClass = UTextBlock::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("Image"), ESearchCase::IgnoreCase))
	{
		WClass = UImage::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("ProgressBar"), ESearchCase::IgnoreCase))
	{
		WClass = UProgressBar::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("CanvasPanel"), ESearchCase::IgnoreCase))
	{
		WClass = UCanvasPanel::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("VerticalBox"), ESearchCase::IgnoreCase))
	{
		WClass = UVerticalBox::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("HorizontalBox"), ESearchCase::IgnoreCase))
	{
		WClass = UHorizontalBox::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("Border"), ESearchCase::IgnoreCase))
	{
		WClass = UBorder::StaticClass();
	}
	else if (WidgetClass.Equals(TEXT("Overlay"), ESearchCase::IgnoreCase))
	{
		WClass = UOverlay::StaticClass();
	}
	else
	{
		WClass = FindFirstObject<UClass>(*WidgetClass, EFindFirstObjectOptions::ExactClass);
		if (!WClass)
		{
			WClass = FindFirstObject<UClass>(*(TEXT("U") + WidgetClass), EFindFirstObjectOptions::ExactClass);
		}
	}

	if (!WClass || !WClass->IsChildOf(UWidget::StaticClass()))
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Widget class not found: %s"), *WidgetClass));
	}

	// Find parent widget
	UPanelWidget* Parent = nullptr;

	if (!ParentWidget.IsEmpty())
	{
		WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName().Equals(ParentWidget, ESearchCase::IgnoreCase))
			{
				Parent = Cast<UPanelWidget>(Widget);
			}
		});

		if (!Parent)
		{
			return FMcpToolResult::Error(FString::Printf(TEXT("Parent widget not found or not a panel: %s"), *ParentWidget));
		}
	}
	else
	{
		Parent = Cast<UPanelWidget>(WidgetTree->RootWidget);
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "AddWidget", "Add {0} to {1}"),
			FText::FromString(WidgetName), FText::FromString(AssetPath)));

	FMcpAssetModifier::MarkModified(WidgetBP);

	// Create the widget
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WClass, FName(*WidgetName));

	if (!NewWidget)
	{
		return FMcpToolResult::Error(TEXT("Failed to create widget"));
	}

	// Add to parent or set as root
	if (Parent)
	{
		Parent->AddChild(NewWidget);
	}
	else
	{
		WidgetTree->RootWidget = NewWidget;
	}

	FMcpAssetModifier::MarkPackageDirty(WidgetBP);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("widget_name"), NewWidget->GetName());
	Result->SetStringField(TEXT("widget_class"), WClass->GetName());
	Result->SetBoolField(TEXT("needs_compile"), true);
	Result->SetBoolField(TEXT("needs_save"), true);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("add-widget: Added %s"), *NewWidget->GetName());

	return FMcpToolResult::Json(Result);
}
