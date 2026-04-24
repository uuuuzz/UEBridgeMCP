// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "WidgetBlueprintTool.generated.h"

class UWidget;
class UPanelSlot;
class UWidgetTree;
class UWidgetBlueprint;
class UCanvasPanelSlot;
class UOverlaySlot;
class UGridSlot;
class UWidgetAnimation;

/**
 * Tool for inspecting Widget Blueprint-specific data including
 * widget hierarchy, slot information, and visibility settings.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UWidgetBlueprintTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("inspect-widget-blueprint"); }
	virtual FString GetToolDescription() const override
	{
		return TEXT("Inspect Widget Blueprint-specific data including widget hierarchy from WidgetTree, "
			"slot information (anchors, offsets, sizes), visibility settings, named slots, property bindings, "
			"and animations. Works only with Widget Blueprints (UserWidget subclasses).");
	}

	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path") }; }

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	/** Build widget hierarchy recursively */
	TSharedPtr<FJsonObject> BuildWidgetNode(
		UWidget* Widget,
		int32 CurrentDepth,
		int32 MaxDepth,
		bool bIncludeDefaults,
		bool bIncludeSlots,
		bool bNamedOnly);

	/** Extract slot information for a widget */
	TSharedPtr<FJsonObject> ExtractSlotInfo(UPanelSlot* Slot);

	/** Extract CanvasPanelSlot-specific properties */
	TSharedPtr<FJsonObject> ExtractCanvasSlotInfo(UCanvasPanelSlot* CanvasSlot);

	/** Extract OverlaySlot properties */
	TSharedPtr<FJsonObject> ExtractOverlaySlotInfo(UOverlaySlot* OverlaySlot);

	/** Extract GridSlot properties */
	TSharedPtr<FJsonObject> ExtractGridSlotInfo(UGridSlot* GridSlot);

	/** Extract common widget properties */
	TSharedPtr<FJsonObject> ExtractWidgetProperties(UWidget* Widget, bool bIncludeDefaults);

	/** Extract property bindings from widget blueprint */
	TArray<TSharedPtr<FJsonValue>> ExtractBindings(UWidgetBlueprint* WidgetBP);

	/** Extract animations from widget blueprint */
	TArray<TSharedPtr<FJsonValue>> ExtractAnimations(UWidgetBlueprint* WidgetBP);

	/** Get visibility as string */
	static FString VisibilityToString(ESlateVisibility Visibility);

	/** Get horizontal alignment as string */
	static FString HAlignToString(EHorizontalAlignment Align);

	/** Get vertical alignment as string */
	static FString VAlignToString(EVerticalAlignment Align);

	/** Collect all widget names for flat listing */
	void CollectWidgetNames(UWidget* Widget, TArray<FString>& OutNames, bool bNamedOnly);

	/** Build a detailed flat widget entry with ancestry metadata */
	TSharedPtr<FJsonObject> BuildFlatWidgetEntry(UWidget* Widget, UWidgetBlueprint* WidgetBP, bool bIncludeSlots);

	/** Whether this widget should be included when named_only is enabled */
	static bool ShouldIncludeNamedWidget(UWidget* Widget);
};
