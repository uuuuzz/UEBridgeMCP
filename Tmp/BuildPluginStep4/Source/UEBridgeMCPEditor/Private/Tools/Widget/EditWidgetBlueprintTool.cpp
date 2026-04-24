// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Widget/EditWidgetBlueprintTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "UEBridgeMCPEditor.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieScene.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

namespace
{
static UClass* ResolveWidgetClass(const FString& WidgetClassName, FString& OutError)
{
	FString ResolveError;
	UClass* ResolvedClass = FMcpPropertySerializer::ResolveClass(WidgetClassName, ResolveError);
	if (!ResolvedClass)
	{
		OutError = ResolveError;
		return nullptr;
	}

	if (!ResolvedClass->IsChildOf(UWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UWidget subclass"), *WidgetClassName);
		return nullptr;
	}

	return ResolvedClass;
}

static bool ParseVector2DValue(const TSharedPtr<FJsonValue>& Value, FVector2D& OutValue)
{
	if (!Value.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!Value->TryGetArray(JsonArray) || !JsonArray || JsonArray->Num() < 2)
	{
		return false;
	}

	OutValue.X = static_cast<float>((*JsonArray)[0]->AsNumber());
	OutValue.Y = static_cast<float>((*JsonArray)[1]->AsNumber());
	return true;
}

static bool ParseFMarginValue(const TSharedPtr<FJsonValue>& Value, FMargin& OutMargin)
{
	if (!Value.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonObject = nullptr;
	if (Value->TryGetObject(JsonObject) && JsonObject && (*JsonObject).IsValid())
	{
		OutMargin.Left = static_cast<float>((*JsonObject)->GetNumberField(TEXT("left")));
		OutMargin.Top = static_cast<float>((*JsonObject)->GetNumberField(TEXT("top")));
		OutMargin.Right = static_cast<float>((*JsonObject)->GetNumberField(TEXT("right")));
		OutMargin.Bottom = static_cast<float>((*JsonObject)->GetNumberField(TEXT("bottom")));
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!Value->TryGetArray(JsonArray) || !JsonArray || JsonArray->Num() < 4)
	{
		return false;
	}

	OutMargin.Left = static_cast<float>((*JsonArray)[0]->AsNumber());
	OutMargin.Top = static_cast<float>((*JsonArray)[1]->AsNumber());
	OutMargin.Right = static_cast<float>((*JsonArray)[2]->AsNumber());
	OutMargin.Bottom = static_cast<float>((*JsonArray)[3]->AsNumber());
	return true;
}

static bool ParseAnchorsValue(const TSharedPtr<FJsonValue>& Value, FAnchors& OutAnchors)
{
	if (!Value.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonObject = nullptr;
	if (Value->TryGetObject(JsonObject) && JsonObject && (*JsonObject).IsValid())
	{
		FVector2D MinVector;
		FVector2D MaxVector;
		TSharedPtr<FJsonValue> MinValue = (*JsonObject)->TryGetField(TEXT("min"));
		TSharedPtr<FJsonValue> MaxValue = (*JsonObject)->TryGetField(TEXT("max"));
		if (!ParseVector2DValue(MinValue, MinVector) || !ParseVector2DValue(MaxValue, MaxVector))
		{
			return false;
		}

		OutAnchors.Minimum = FVector2D(MinVector.X, MinVector.Y);
		OutAnchors.Maximum = FVector2D(MaxVector.X, MaxVector.Y);
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!Value->TryGetArray(JsonArray) || !JsonArray || JsonArray->Num() < 4)
	{
		return false;
	}

	OutAnchors.Minimum = FVector2D(
		static_cast<float>((*JsonArray)[0]->AsNumber()),
		static_cast<float>((*JsonArray)[1]->AsNumber()));
	OutAnchors.Maximum = FVector2D(
		static_cast<float>((*JsonArray)[2]->AsNumber()),
		static_cast<float>((*JsonArray)[3]->AsNumber()));
	return true;
}

static bool ApplyPropertyObject(
	UObject* TargetObject,
	const TSharedPtr<FJsonObject>& PropertiesObject,
	bool bApply,
	FString& OutError)
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

static bool ApplySlotObject(
	UWidget* Widget,
	const TSharedPtr<FJsonObject>& SlotObject,
	bool bApply,
	FString& OutError)
{
	if (!Widget)
	{
		OutError = TEXT("Target widget is null");
		return false;
	}

	if (!SlotObject.IsValid())
	{
		OutError = TEXT("'slot' must be an object");
		return false;
	}

	UPanelSlot* Slot = Widget->Slot;
	if (!Slot)
	{
		OutError = FString::Printf(TEXT("Widget '%s' has no parent slot"), *Widget->GetName());
		return false;
	}

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		if (const TSharedPtr<FJsonValue> AnchorsValue = SlotObject->TryGetField(TEXT("anchors")))
		{
			FAnchors Anchors;
			if (!ParseAnchorsValue(AnchorsValue, Anchors))
			{
				OutError = TEXT("Invalid 'anchors' value. Use [minX,minY,maxX,maxY] or {min:[x,y], max:[x,y]}");
				return false;
			}

			if (bApply)
			{
				CanvasSlot->SetAnchors(Anchors);
			}
		}

		if (const TSharedPtr<FJsonValue> OffsetsValue = SlotObject->TryGetField(TEXT("offsets")))
		{
			FMargin Offsets;
			if (!ParseFMarginValue(OffsetsValue, Offsets))
			{
				OutError = TEXT("Invalid 'offsets' value. Use [left,top,right,bottom] or {left,top,right,bottom}");
				return false;
			}

			if (bApply)
			{
				CanvasSlot->SetOffsets(Offsets);
			}
		}

		if (const TSharedPtr<FJsonValue> PositionValue = SlotObject->TryGetField(TEXT("position")))
		{
			FVector2D Position;
			if (!ParseVector2DValue(PositionValue, Position))
			{
				OutError = TEXT("Invalid 'position' value. Use [x,y]");
				return false;
			}

			if (bApply)
			{
				CanvasSlot->SetPosition(Position);
			}
		}

		if (const TSharedPtr<FJsonValue> SizeValue = SlotObject->TryGetField(TEXT("size")))
		{
			FVector2D Size;
			if (!ParseVector2DValue(SizeValue, Size))
			{
				OutError = TEXT("Invalid 'size' value. Use [x,y]");
				return false;
			}

			if (bApply)
			{
				CanvasSlot->SetSize(Size);
			}
		}

		if (const TSharedPtr<FJsonValue> AlignmentValue = SlotObject->TryGetField(TEXT("alignment")))
		{
			FVector2D Alignment;
			if (!ParseVector2DValue(AlignmentValue, Alignment))
			{
				OutError = TEXT("Invalid 'alignment' value. Use [x,y]");
				return false;
			}

			if (bApply)
			{
				CanvasSlot->SetAlignment(Alignment);
			}
		}

		bool bAutoSize = false;
		if (SlotObject->TryGetBoolField(TEXT("auto_size"), bAutoSize) && bApply)
		{
			CanvasSlot->SetAutoSize(bAutoSize);
		}

		double ZOrder = 0.0;
		if (SlotObject->TryGetNumberField(TEXT("z_order"), ZOrder) && bApply)
		{
			CanvasSlot->SetZOrder(static_cast<int32>(ZOrder));
		}
	}

	static const TSet<FString> CanvasKeys = {
		TEXT("anchors"),
		TEXT("offsets"),
		TEXT("position"),
		TEXT("size"),
		TEXT("alignment"),
		TEXT("auto_size"),
		TEXT("z_order")
	};

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SlotObject->Values)
	{
		if (CanvasKeys.Contains(Pair.Key))
		{
			continue;
		}

		FProperty* Property = nullptr;
		void* Container = nullptr;
		FString PropertyError;
		if (!FMcpAssetModifier::FindPropertyByPath(Slot, Pair.Key, Property, Container, PropertyError))
		{
			OutError = FString::Printf(TEXT("Slot property '%s' not found: %s"), *Pair.Key, *PropertyError);
			return false;
		}

		if (bApply && !FMcpAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, PropertyError))
		{
			OutError = FString::Printf(TEXT("Failed to set slot property '%s': %s"), *Pair.Key, *PropertyError);
			return false;
		}
	}

	return true;
}

static bool DeleteWidgetWithoutTransaction(UWidgetBlueprint* Blueprint, UWidget* WidgetTemplate, FString& OutError)
{
	if (!Blueprint || !Blueprint->WidgetTree || !WidgetTemplate)
	{
		OutError = TEXT("Invalid widget delete request");
		return false;
	}

	Blueprint->WidgetTree->SetFlags(RF_Transactional);
	Blueprint->WidgetTree->Modify();
	Blueprint->Modify();

	const FName WidgetName = WidgetTemplate->GetFName();

	for (int32 BindingIndex = Blueprint->Bindings.Num() - 1; BindingIndex >= 0; --BindingIndex)
	{
		FDelegateEditorBinding& Binding = Blueprint->Bindings[BindingIndex];
		if (Binding.ObjectName == WidgetTemplate->GetName())
		{
			Blueprint->Bindings.RemoveAt(BindingIndex);
		}
	}

	if (UPanelWidget* Parent = WidgetTemplate->GetParent())
	{
		Parent->SetFlags(RF_Transactional);
		Parent->Modify();
	}

	WidgetTemplate->SetFlags(RF_Transactional);
	WidgetTemplate->Modify();

	bool bRemoved = Blueprint->WidgetTree->RemoveWidget(WidgetTemplate);
	if (WidgetTemplate->GetParent() == nullptr)
	{
		TScriptInterface<INamedSlotInterface> NamedSlotHost =
			FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(WidgetTemplate, Blueprint->WidgetTree);
		if (NamedSlotHost)
		{
			bRemoved |= FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(WidgetTemplate, NamedSlotHost);
		}
	}

	FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, WidgetTemplate->GetFName());
	FWidgetBlueprintEditorUtils::ReplaceDesiredFocus(Blueprint, WidgetTemplate->GetFName(), FName());
	WidgetTemplate->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);

	const bool bHasWidgetWithSameName = Blueprint->GetAllSourceWidgets().ContainsByPredicate(
		[WidgetName](const UWidget* Widget)
		{
			return Widget && Widget->GetFName() == WidgetName;
		});
	if (!bHasWidgetWithSameName)
	{
		Blueprint->OnVariableRemoved(WidgetName);
	}

	TArray<UWidget*> ChildWidgets;
	UWidgetTree::GetChildWidgets(WidgetTemplate, ChildWidgets);
	for (UWidget* ChildWidget : ChildWidgets)
	{
		if (!ChildWidget)
		{
			continue;
		}

		const FName ChildWidgetName = ChildWidget->GetFName();
		ChildWidget->SetFlags(RF_Transactional);
		ChildWidget->Modify();

		FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, ChildWidget->GetFName());
		ChildWidget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);

		const bool bHasChildWithSameName = Blueprint->GetAllSourceWidgets().ContainsByPredicate(
			[ChildWidgetName](const UWidget* Widget)
			{
				return Widget && Widget->GetFName() == ChildWidgetName;
			});
		if (!bHasChildWithSameName)
		{
			Blueprint->OnVariableRemoved(ChildWidgetName);
		}
	}

	if (!bRemoved)
	{
		OutError = FString::Printf(TEXT("Failed to remove widget '%s' from WidgetTree"), *WidgetName.ToString());
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

static bool RenameWidgetWithoutTransaction(UWidgetBlueprint* Blueprint, UWidget* Widget, const FString& NewDisplayName, FString& OutError)
{
	if (!Blueprint || !Blueprint->WidgetTree || !Widget)
	{
		OutError = TEXT("Invalid widget rename request");
		return false;
	}

	const FName OldName = Widget->GetFName();
	const FName NewName = FName(*NewDisplayName);

	if (OldName == NewName)
	{
		return true;
	}

	if (UWidget* ExistingWidget = Blueprint->WidgetTree->FindWidget(NewName))
	{
		if (ExistingWidget != Widget)
		{
			OutError = FString::Printf(TEXT("Widget '%s' already exists"), *NewDisplayName);
			return false;
		}
	}

	Blueprint->Modify();
	Widget->Modify();
	Blueprint->OnVariableRenamed(OldName, NewName);

	Widget->SetDisplayLabel(NewDisplayName);
	Widget->Rename(*NewDisplayName, nullptr, REN_DontCreateRedirectors);

	FWidgetBlueprintEditorUtils::ReplaceDesiredFocus(Blueprint, OldName, NewName);

	for (FDelegateEditorBinding& Binding : Blueprint->Bindings)
	{
		if (Binding.ObjectName == OldName.ToString())
		{
			Binding.ObjectName = NewDisplayName;
		}
	}

	for (UWidgetAnimation* WidgetAnimation : Blueprint->Animations)
	{
		if (!WidgetAnimation || !WidgetAnimation->MovieScene)
		{
			continue;
		}

		for (FWidgetAnimationBinding& AnimationBinding : WidgetAnimation->AnimationBindings)
		{
			if (AnimationBinding.WidgetName == OldName)
			{
				AnimationBinding.WidgetName = NewName;

				if (AnimationBinding.SlotWidgetName == NAME_None)
				{
					if (FMovieScenePossessable* Possessable = WidgetAnimation->MovieScene->FindPossessable(AnimationBinding.AnimationGuid))
					{
						WidgetAnimation->MovieScene->Modify();
						Possessable->SetName(NewDisplayName);
					}
				}
			}
		}
	}

	Blueprint->WidgetTree->ForEachWidget([OldName, NewName](UWidget* ExistingWidget)
	{
		if (ExistingWidget && ExistingWidget->Navigation)
		{
			ExistingWidget->Navigation->SetFlags(RF_Transactional);
			ExistingWidget->Navigation->Modify();
			ExistingWidget->Navigation->TryToRenameBinding(OldName, NewName);
		}
	});

	FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, NewName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint, OldName, NewName);
	return true;
}

static UWidget* FindWidgetByName(UWidgetBlueprint* Blueprint, const FString& WidgetName)
{
	return Blueprint && Blueprint->WidgetTree ? Blueprint->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
}
}

FString UEditWidgetBlueprintTool::GetToolDescription() const
{
	return TEXT("Transactional Widget Blueprint editing with batched operations for widget tree structure, "
		"default properties, and slot layout. Supports dry_run, compile, save, and rollback_on_error.");
}

TMap<FString, FMcpSchemaProperty> UEditWidgetBlueprintTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Widget Blueprint asset path"),
		true));

	TSharedPtr<FJsonObject> GenericObjectRawSchema = MakeShareable(new FJsonObject);
	GenericObjectRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	GenericObjectRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> GenericObjectSchema = MakeShared<FMcpSchemaProperty>();
	GenericObjectSchema->Description = TEXT("Generic property override object");
	GenericObjectSchema->RawSchema = GenericObjectRawSchema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Widget Blueprint edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Widget edit action"),
		{ TEXT("add_widget"), TEXT("remove_widget"), TEXT("rename_widget"), TEXT("reparent_widget"), TEXT("set_widget_properties"), TEXT("set_slot_properties") },
		true)));
	OperationSchema->Properties.Add(TEXT("widget_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Target widget name"))));
	OperationSchema->Properties.Add(TEXT("widget_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Widget class path or short class name for add_widget"))));
	OperationSchema->Properties.Add(TEXT("parent_widget"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Parent panel widget name for add_widget or reparent_widget"))));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("New widget name for rename_widget"))));
	OperationSchema->Properties.Add(TEXT("make_root"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("boolean"),
		TEXT("Create the new widget as the root widget"))));
	OperationSchema->Properties.Add(TEXT("is_variable"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("boolean"),
		TEXT("Whether the new widget should be marked as a Blueprint variable"))));
	OperationSchema->Properties.Add(TEXT("properties"), GenericObjectSchema);
	OperationSchema->Properties.Add(TEXT("slot"), GenericObjectSchema);

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Array of widget edit operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy: 'never', 'if_needed', or 'always'"),
		{ TEXT("never"), TEXT("if_needed"), TEXT("always") }));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after successful edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));

	return Schema;
}

FMcpToolResult UEditWidgetBlueprintTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), OperationsArray) || !OperationsArray || OperationsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("if_needed"));
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Widget Blueprint"));

	FString LoadError;
	UWidgetBlueprint* WidgetBlueprint = FMcpAssetModifier::LoadAssetByPath<UWidgetBlueprint>(AssetPath, LoadError);
	if (!WidgetBlueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	if (!WidgetBlueprint->WidgetTree)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"),
			TEXT("Widget Blueprint has no WidgetTree"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
		WidgetBlueprint->Modify();
		WidgetBlueprint->WidgetTree->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < OperationsArray->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonValue>& OperationValue = (*OperationsArray)[OperationIndex];
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!OperationValue.IsValid() || !OperationValue->TryGetObject(OperationObject) || !(*OperationObject).IsValid())
		{
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Operation at index %d is not a valid object"), OperationIndex));
			}
			continue;
		}

		TSharedPtr<FJsonObject> OperationResult = MakeShareable(new FJsonObject);
		OperationResult->SetNumberField(TEXT("index"), OperationIndex);

		FString ActionName;
		if (!(*OperationObject)->TryGetStringField(TEXT("action"), ActionName))
		{
			OperationResult->SetBoolField(TEXT("success"), false);
			OperationResult->SetStringField(TEXT("error"), TEXT("Missing 'action' field"));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
			bAnyFailed = true;
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Operation at index %d is missing 'action'"), OperationIndex));
			}
			continue;
		}

		OperationResult->SetStringField(TEXT("action"), ActionName);
		FString OperationError;
		bool bOperationSuccess = false;
		bool bOperationChanged = false;

		if (ActionName == TEXT("add_widget"))
		{
			FString WidgetName;
			FString WidgetClassName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_name"), WidgetName) ||
				!(*OperationObject)->TryGetStringField(TEXT("widget_class"), WidgetClassName))
			{
				OperationError = TEXT("'widget_name' and 'widget_class' are required for add_widget");
			}
			else if (FindWidgetByName(WidgetBlueprint, WidgetName))
			{
				OperationError = FString::Printf(TEXT("Widget '%s' already exists"), *WidgetName);
			}
			else
			{
				FString ResolveError;
				UClass* WidgetClass = ResolveWidgetClass(WidgetClassName, ResolveError);
				if (!WidgetClass)
				{
					OperationError = ResolveError;
				}
				else
				{
					const FString ParentWidgetName = GetStringArgOrDefault(*OperationObject, TEXT("parent_widget"));
					const bool bMakeRoot = GetBoolArgOrDefault(*OperationObject, TEXT("make_root"), false);
					const bool bIsVariable = GetBoolArgOrDefault(*OperationObject, TEXT("is_variable"), true);
					UPanelWidget* ParentPanel = nullptr;

					if (!ParentWidgetName.IsEmpty())
					{
						UWidget* ParentWidget = FindWidgetByName(WidgetBlueprint, ParentWidgetName);
						ParentPanel = Cast<UPanelWidget>(ParentWidget);
						if (!ParentPanel)
						{
							OperationError = FString::Printf(TEXT("Parent widget '%s' was not found or is not a PanelWidget"), *ParentWidgetName);
						}
					}

					if (OperationError.IsEmpty())
					{
						if (!bMakeRoot && ParentPanel == nullptr && WidgetBlueprint->WidgetTree->RootWidget != nullptr)
						{
							ParentPanel = Cast<UPanelWidget>(WidgetBlueprint->WidgetTree->RootWidget);
							if (!ParentPanel)
							{
								OperationError = TEXT("Root widget is not a panel. Provide 'parent_widget' or set 'make_root' to true");
							}
						}
					}

					if (OperationError.IsEmpty())
					{
						OperationResult->SetStringField(TEXT("widget_name"), WidgetName);
						OperationResult->SetStringField(TEXT("widget_class"), WidgetClass->GetPathName());
						if (ParentPanel)
						{
							OperationResult->SetStringField(TEXT("parent_widget"), ParentPanel->GetName());
						}
						OperationResult->SetBoolField(TEXT("is_variable"), bIsVariable);

						if (!bDryRun)
						{
							UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
							if (!NewWidget)
							{
								OperationError = FString::Printf(TEXT("Failed to construct widget '%s'"), *WidgetName);
							}
							else
							{
								NewWidget->Modify();
								NewWidget->SetDisplayLabel(WidgetName);
								NewWidget->bIsVariable = bIsVariable;

								if (bMakeRoot || WidgetBlueprint->WidgetTree->RootWidget == nullptr)
								{
									WidgetBlueprint->WidgetTree->RootWidget = NewWidget;
								}
								else
								{
									ParentPanel->AddChild(NewWidget);
								}

								WidgetBlueprint->OnVariableAdded(NewWidget->GetFName());

								if (const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
									(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && (*PropertiesObject).IsValid())
								{
									if (!ApplyPropertyObject(NewWidget, *PropertiesObject, true, OperationError))
									{
										bOperationSuccess = false;
									}
								}

								if (OperationError.IsEmpty())
								{
									if (const TSharedPtr<FJsonObject>* SlotObject = nullptr;
										(*OperationObject)->TryGetObjectField(TEXT("slot"), SlotObject) && SlotObject && (*SlotObject).IsValid())
									{
										if (!ApplySlotObject(NewWidget, *SlotObject, true, OperationError))
										{
											bOperationSuccess = false;
										}
									}
								}

								if (OperationError.IsEmpty())
								{
									FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
								}
							}
						}
						else
						{
							if (const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
								(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && (*PropertiesObject).IsValid())
							{
								UObject* TransientValidationWidget = NewObject<UWidget>(GetTransientPackage(), WidgetClass, NAME_None, RF_Transient);
								if (!ApplyPropertyObject(TransientValidationWidget, *PropertiesObject, false, OperationError))
								{
									TransientValidationWidget = nullptr;
								}
							}
						}

						if (OperationError.IsEmpty())
						{
							bOperationSuccess = true;
							bOperationChanged = true;
						}
					}
				}
			}
		}
		else if (ActionName == TEXT("remove_widget"))
		{
			FString WidgetName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_name"), WidgetName))
			{
				OperationError = TEXT("'widget_name' is required for remove_widget");
			}
			else
			{
				UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
				if (!Widget)
				{
					OperationError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName);
				}
				else
				{
					OperationResult->SetStringField(TEXT("widget_name"), WidgetName);
					if (!bDryRun)
					{
						bOperationSuccess = DeleteWidgetWithoutTransaction(WidgetBlueprint, Widget, OperationError);
					}
					else
					{
						bOperationSuccess = true;
					}
					bOperationChanged = bOperationSuccess;
				}
			}
		}
		else if (ActionName == TEXT("rename_widget"))
		{
			FString WidgetName;
			FString NewName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_name"), WidgetName) ||
				!(*OperationObject)->TryGetStringField(TEXT("new_name"), NewName))
			{
				OperationError = TEXT("'widget_name' and 'new_name' are required for rename_widget");
			}
			else
			{
				UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
				if (!Widget)
				{
					OperationError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName);
				}
				else
				{
					OperationResult->SetStringField(TEXT("widget_name"), WidgetName);
					OperationResult->SetStringField(TEXT("new_name"), NewName);
					if (!bDryRun)
					{
						bOperationSuccess = RenameWidgetWithoutTransaction(WidgetBlueprint, Widget, NewName, OperationError);
					}
					else
					{
						if (UWidget* ExistingWidget = FindWidgetByName(WidgetBlueprint, NewName))
						{
							if (ExistingWidget != Widget)
							{
								OperationError = FString::Printf(TEXT("Widget '%s' already exists"), *NewName);
							}
						}
						bOperationSuccess = OperationError.IsEmpty();
					}
					bOperationChanged = bOperationSuccess && WidgetName != NewName;
				}
			}
		}
		else if (ActionName == TEXT("reparent_widget"))
		{
			FString WidgetName;
			FString ParentWidgetName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_name"), WidgetName) ||
				!(*OperationObject)->TryGetStringField(TEXT("parent_widget"), ParentWidgetName))
			{
				OperationError = TEXT("'widget_name' and 'parent_widget' are required for reparent_widget");
			}
			else
			{
				UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
				UPanelWidget* NewParent = Cast<UPanelWidget>(FindWidgetByName(WidgetBlueprint, ParentWidgetName));
				if (!Widget)
				{
					OperationError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName);
				}
				else if (!NewParent)
				{
					OperationError = FString::Printf(TEXT("Parent widget '%s' was not found or is not a PanelWidget"), *ParentWidgetName);
				}
				else if (Widget == WidgetBlueprint->WidgetTree->RootWidget)
				{
					OperationError = TEXT("Root widget cannot be reparented");
				}
				else
				{
					TArray<UWidget*> Descendants;
					UWidgetTree::GetChildWidgets(Widget, Descendants);
					if (Descendants.Contains(NewParent))
					{
						OperationError = TEXT("Cannot reparent a widget under one of its descendants");
					}
					else
					{
						int32 ChildIndex = INDEX_NONE;
						UPanelWidget* OldParent = UWidgetTree::FindWidgetParent(Widget, ChildIndex);
						if (!OldParent)
						{
							OperationError = TEXT("Current parent panel was not found");
						}
						else
						{
							OperationResult->SetStringField(TEXT("widget_name"), WidgetName);
							OperationResult->SetStringField(TEXT("old_parent"), OldParent->GetName());
							OperationResult->SetStringField(TEXT("new_parent"), NewParent->GetName());

							if (!bDryRun)
							{
								OldParent->Modify();
								NewParent->Modify();
								Widget->Modify();
								if (!OldParent->RemoveChild(Widget))
								{
									OperationError = FString::Printf(TEXT("Failed to detach '%s' from '%s'"), *WidgetName, *OldParent->GetName());
								}
								else
								{
									NewParent->AddChild(Widget);
									FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
									bOperationSuccess = true;
								}
							}
							else
							{
								bOperationSuccess = true;
							}

							bOperationChanged = bOperationSuccess && OldParent != NewParent;
						}
					}
				}
			}
		}
		else if (ActionName == TEXT("set_widget_properties"))
		{
			FString WidgetName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_name"), WidgetName))
			{
				OperationError = TEXT("'widget_name' is required for set_widget_properties");
			}
			else
			{
				UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
				const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
				if (!Widget)
				{
					OperationError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName);
				}
				else if (!(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !(*PropertiesObject).IsValid())
				{
					OperationError = TEXT("'properties' object is required for set_widget_properties");
				}
				else
				{
					OperationResult->SetStringField(TEXT("widget_name"), WidgetName);
					bOperationSuccess = ApplyPropertyObject(Widget, *PropertiesObject, !bDryRun, OperationError);
					if (bOperationSuccess && !bDryRun)
					{
						FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
					}
					bOperationChanged = bOperationSuccess;
				}
			}
		}
		else if (ActionName == TEXT("set_slot_properties"))
		{
			FString WidgetName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_name"), WidgetName))
			{
				OperationError = TEXT("'widget_name' is required for set_slot_properties");
			}
			else
			{
				UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
				const TSharedPtr<FJsonObject>* SlotObject = nullptr;
				if (!Widget)
				{
					OperationError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName);
				}
				else if (!(*OperationObject)->TryGetObjectField(TEXT("slot"), SlotObject) || !SlotObject || !(*SlotObject).IsValid())
				{
					OperationError = TEXT("'slot' object is required for set_slot_properties");
				}
				else
				{
					OperationResult->SetStringField(TEXT("widget_name"), WidgetName);
					bOperationSuccess = ApplySlotObject(Widget, *SlotObject, !bDryRun, OperationError);
					if (bOperationSuccess && !bDryRun)
					{
						FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
					}
					bOperationChanged = bOperationSuccess;
				}
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		OperationResult->SetBoolField(TEXT("success"), bOperationSuccess);
		OperationResult->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!OperationError.IsEmpty())
		{
			OperationResult->SetStringField(TEXT("error"), OperationError);
		}

		if (!bOperationSuccess)
		{
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));

			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("asset_path"), AssetPath);
				Details->SetNumberField(TEXT("failed_operation_index"), OperationIndex);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), OperationError, Details);
			}
		}
		else
		{
			bAnyChanged |= bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
	}

	if (!bDryRun && bAnyChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(WidgetBlueprint);
	}

	bool bCompileAttempted = false;
	bool bCompileSuccess = true;
	FString CompileError;
	if (!bDryRun && bAnyChanged)
	{
		const bool bShouldCompile = (CompilePolicy == TEXT("always")) ||
			(CompilePolicy == TEXT("if_needed"));
		if (bShouldCompile)
		{
			bCompileAttempted = true;
			FMcpAssetModifier::RefreshBlueprintNodes(WidgetBlueprint);
			bCompileSuccess = FMcpAssetModifier::CompileBlueprint(WidgetBlueprint, CompileError);
			if (!bCompileSuccess)
			{
				TSharedPtr<FJsonObject> DiagnosticObject = MakeShareable(new FJsonObject);
				DiagnosticObject->SetStringField(TEXT("severity"), TEXT("error"));
				DiagnosticObject->SetStringField(TEXT("code"), TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"));
				DiagnosticObject->SetStringField(TEXT("message"), CompileError);
				DiagnosticObject->SetStringField(TEXT("asset_path"), AssetPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(DiagnosticObject)));
				bAnyFailed = true;
			}
		}
	}

	if (!bDryRun && bSave && !bAnyFailed)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(WidgetBlueprint, false, SaveError) && !SaveError.IsEmpty())
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Save failed: %s"), *SaveError))));
		}
	}

	TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
	CompileObject->SetBoolField(TEXT("attempted"), bCompileAttempted);
	CompileObject->SetBoolField(TEXT("success"), bCompileSuccess);
	if (!CompileError.IsEmpty())
	{
		CompileObject->SetStringField(TEXT("error"), CompileError);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	ModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
