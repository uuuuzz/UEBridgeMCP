// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Widget/EditWidgetComponentTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "Components/WidgetComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace
{
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

static UClass* ResolveUserWidgetClass(const FString& ClassName, FString& OutError)
{
	FString ResolveError;
	UClass* ResolvedClass = FMcpPropertySerializer::ResolveClass(ClassName, ResolveError);
	if (!ResolvedClass)
	{
		OutError = ResolveError;
		return nullptr;
	}

	if (!ResolvedClass->IsChildOf(UUserWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' must inherit from UUserWidget"), *ClassName);
		return nullptr;
	}

	return ResolvedClass;
}

static bool ParseWidgetSpace(const TSharedPtr<FJsonValue>& Value, EWidgetSpace& OutSpace)
{
	FString StringValue;
	if (Value.IsValid() && Value->TryGetString(StringValue))
	{
		if (StringValue.Equals(TEXT("World"), ESearchCase::IgnoreCase))
		{
			OutSpace = EWidgetSpace::World;
			return true;
		}
		if (StringValue.Equals(TEXT("Screen"), ESearchCase::IgnoreCase))
		{
			OutSpace = EWidgetSpace::Screen;
			return true;
		}
	}
	return false;
}

static bool ParseBlendMode(const TSharedPtr<FJsonValue>& Value, EWidgetBlendMode& OutBlendMode)
{
	FString StringValue;
	if (Value.IsValid() && Value->TryGetString(StringValue))
	{
		if (StringValue.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase))
		{
			OutBlendMode = EWidgetBlendMode::Opaque;
			return true;
		}
		if (StringValue.Equals(TEXT("Masked"), ESearchCase::IgnoreCase))
		{
			OutBlendMode = EWidgetBlendMode::Masked;
			return true;
		}
		if (StringValue.Equals(TEXT("Transparent"), ESearchCase::IgnoreCase))
		{
			OutBlendMode = EWidgetBlendMode::Transparent;
			return true;
		}
	}
	return false;
}

static bool ParseGeometryMode(const TSharedPtr<FJsonValue>& Value, EWidgetGeometryMode& OutGeometryMode)
{
	FString StringValue;
	if (Value.IsValid() && Value->TryGetString(StringValue))
	{
		if (StringValue.Equals(TEXT("Plane"), ESearchCase::IgnoreCase))
		{
			OutGeometryMode = EWidgetGeometryMode::Plane;
			return true;
		}
		if (StringValue.Equals(TEXT("Cylinder"), ESearchCase::IgnoreCase))
		{
			OutGeometryMode = EWidgetGeometryMode::Cylinder;
			return true;
		}
	}
	return false;
}

static bool ParseWindowVisibility(const TSharedPtr<FJsonValue>& Value, EWindowVisibility& OutVisibility)
{
	FString StringValue;
	if (Value.IsValid() && Value->TryGetString(StringValue))
	{
		if (StringValue.Equals(TEXT("Visible"), ESearchCase::IgnoreCase))
		{
			OutVisibility = EWindowVisibility::Visible;
			return true;
		}
		if (StringValue.Equals(TEXT("SelfHitTestInvisible"), ESearchCase::IgnoreCase))
		{
			OutVisibility = EWindowVisibility::SelfHitTestInvisible;
			return true;
		}
	}
	return false;
}

static bool ParseTickMode(const TSharedPtr<FJsonValue>& Value, ETickMode& OutTickMode)
{
	FString StringValue;
	if (Value.IsValid() && Value->TryGetString(StringValue))
	{
		if (StringValue.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase))
		{
			OutTickMode = ETickMode::Disabled;
			return true;
		}
		if (StringValue.Equals(TEXT("Enabled"), ESearchCase::IgnoreCase))
		{
			OutTickMode = ETickMode::Enabled;
			return true;
		}
		if (StringValue.Equals(TEXT("Automatic"), ESearchCase::IgnoreCase))
		{
			OutTickMode = ETickMode::Automatic;
			return true;
		}
	}
	return false;
}

static bool ApplyWidgetComponentProperty(
	UWidgetComponent* WidgetComponent,
	const FString& PropertyPath,
	const TSharedPtr<FJsonValue>& PropertyValue,
	bool bApply,
	FString& OutError)
{
	if (!WidgetComponent)
	{
		OutError = TEXT("WidgetComponent target is null");
		return false;
	}

	if (PropertyPath.Equals(TEXT("widget_class"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("WidgetClass"), ESearchCase::IgnoreCase))
	{
		FString WidgetClassName;
		if (!PropertyValue.IsValid() || !PropertyValue->TryGetString(WidgetClassName))
		{
			OutError = TEXT("'widget_class' must be a string");
			return false;
		}

		FString ResolveError;
		UClass* ResolvedClass = ResolveUserWidgetClass(WidgetClassName, ResolveError);
		if (!ResolvedClass)
		{
			OutError = ResolveError;
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetWidgetClass(ResolvedClass);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("draw_size"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("DrawSize"), ESearchCase::IgnoreCase))
	{
		FVector2D DrawSize;
		if (!ParseVector2DValue(PropertyValue, DrawSize))
		{
			OutError = TEXT("'draw_size' must be [width,height]");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetDrawSize(DrawSize);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("pivot"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("Pivot"), ESearchCase::IgnoreCase))
	{
		FVector2D Pivot;
		if (!ParseVector2DValue(PropertyValue, Pivot))
		{
			OutError = TEXT("'pivot' must be [x,y]");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetPivot(Pivot);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("space"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("Space"), ESearchCase::IgnoreCase))
	{
		EWidgetSpace Space;
		if (!ParseWidgetSpace(PropertyValue, Space))
		{
			OutError = TEXT("'space' must be 'World' or 'Screen'");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetWidgetSpace(Space);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("blend_mode"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("BlendMode"), ESearchCase::IgnoreCase))
	{
		EWidgetBlendMode BlendMode;
		if (!ParseBlendMode(PropertyValue, BlendMode))
		{
			OutError = TEXT("'blend_mode' must be 'Opaque', 'Masked', or 'Transparent'");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetBlendMode(BlendMode);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("geometry_mode"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("GeometryMode"), ESearchCase::IgnoreCase))
	{
		EWidgetGeometryMode GeometryMode;
		if (!ParseGeometryMode(PropertyValue, GeometryMode))
		{
			OutError = TEXT("'geometry_mode' must be 'Plane' or 'Cylinder'");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetGeometryMode(GeometryMode);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("window_visibility"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("WindowVisibility"), ESearchCase::IgnoreCase))
	{
		EWindowVisibility Visibility;
		if (!ParseWindowVisibility(PropertyValue, Visibility))
		{
			OutError = TEXT("'window_visibility' must be 'Visible' or 'SelfHitTestInvisible'");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetWindowVisibility(Visibility);
		}
		return true;
	}

	if (PropertyPath.Equals(TEXT("tick_mode"), ESearchCase::IgnoreCase) ||
		PropertyPath.Equals(TEXT("TickMode"), ESearchCase::IgnoreCase))
	{
		ETickMode TickMode;
		if (!ParseTickMode(PropertyValue, TickMode))
		{
			OutError = TEXT("'tick_mode' must be 'Disabled', 'Enabled', or 'Automatic'");
			return false;
		}

		if (bApply)
		{
			WidgetComponent->SetTickMode(TickMode);
		}
		return true;
	}

	FProperty* Property = nullptr;
	void* Container = nullptr;
	FString PropertyError;
	if (!FMcpAssetModifier::FindPropertyByPath(WidgetComponent, PropertyPath, Property, Container, PropertyError))
	{
		OutError = FString::Printf(TEXT("Property '%s' not found: %s"), *PropertyPath, *PropertyError);
		return false;
	}

	if (bApply && !FMcpAssetModifier::SetPropertyFromJson(Property, Container, PropertyValue, PropertyError))
	{
		OutError = FString::Printf(TEXT("Failed to set property '%s': %s"), *PropertyPath, *PropertyError);
		return false;
	}

	return true;
}

static USCS_Node* FindWidgetComponentNode(UBlueprint* Blueprint, const FString& ComponentName)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return nullptr;
	}

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate || !Node->ComponentTemplate->IsA<UWidgetComponent>())
		{
			continue;
		}

		if (ComponentName.IsEmpty() ||
			Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) ||
			Node->ComponentTemplate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	return nullptr;
}

static int32 CountWidgetComponentNodes(UBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return 0;
	}

	int32 Count = 0;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<UWidgetComponent>())
		{
			++Count;
		}
	}
	return Count;
}

static UWidgetComponent* FindWidgetComponentOnActor(AActor* Actor, const FString& ComponentName, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("Actor target is null");
		return nullptr;
	}

	TArray<UWidgetComponent*> WidgetComponents;
	Actor->GetComponents<UWidgetComponent>(WidgetComponents);
	if (WidgetComponents.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Actor '%s' has no WidgetComponent"), *Actor->GetActorNameOrLabel());
		return nullptr;
	}

	if (!ComponentName.IsEmpty())
	{
		for (UWidgetComponent* WidgetComponent : WidgetComponents)
		{
			if (WidgetComponent && WidgetComponent->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return WidgetComponent;
			}
		}

		OutError = FString::Printf(TEXT("WidgetComponent '%s' was not found on actor '%s'"), *ComponentName, *Actor->GetActorNameOrLabel());
		return nullptr;
	}

	if (WidgetComponents.Num() > 1)
	{
		OutError = TEXT("Multiple WidgetComponents found. Specify 'component_name'");
		return nullptr;
	}

	return WidgetComponents[0];
}
}

FString UEditWidgetComponentTool::GetToolDescription() const
{
	return TEXT("Edit WidgetComponent settings on either a Blueprint component template or an editor/PIE actor instance. "
		"Supports batched property updates and explicit widget class assignment.");
}

TMap<FString, FMcpSchemaProperty> UEditWidgetComponentTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("blueprint_path"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Blueprint asset path when editing a WidgetComponent template")));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Actor name/label when editing a placed actor instance")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Optional WidgetComponent name. Required if multiple WidgetComponents exist.")));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("World selection for actor targets"),
		{ TEXT("auto"), TEXT("editor"), TEXT("pie") }));

	TSharedPtr<FJsonObject> GenericObjectRawSchema = MakeShareable(new FJsonObject);
	GenericObjectRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	GenericObjectRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> PropertiesSchema = MakeShared<FMcpSchemaProperty>();
	PropertiesSchema->Description = TEXT("WidgetComponent property overrides");
	PropertiesSchema->RawSchema = GenericObjectRawSchema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("WidgetComponent edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("WidgetComponent edit action"),
		{ TEXT("set_widget_class"), TEXT("set_property"), TEXT("set_properties") },
		true)));
	OperationSchema->Properties.Add(TEXT("widget_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("UUserWidget class path or short name for set_widget_class"))));
	OperationSchema->Properties.Add(TEXT("property"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Single property path for set_property"))));
	OperationSchema->Properties.Add(TEXT("value"), PropertiesSchema);
	OperationSchema->Properties.Add(TEXT("properties"), PropertiesSchema);

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Array of WidgetComponent edit operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy for blueprint targets: 'never', 'if_needed', or 'always'"),
		{ TEXT("never"), TEXT("if_needed"), TEXT("always") }));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save blueprint targets after successful edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));

	return Schema;
}

FMcpToolResult UEditWidgetComponentTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	const FString BlueprintPath = GetStringArgOrDefault(Arguments, TEXT("blueprint_path"));
	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), OperationsArray) || !OperationsArray || OperationsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	if (BlueprintPath.IsEmpty() == ActorName.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARGUMENT"),
			TEXT("Provide exactly one of 'blueprint_path' or 'actor_name'"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("if_needed"));
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Widget Component"));

	UWidgetComponent* WidgetComponent = nullptr;
	UBlueprint* Blueprint = nullptr;
	AActor* Actor = nullptr;
	bool bIsBlueprintTarget = false;

	if (!BlueprintPath.IsEmpty())
	{
		FString LoadError;
		Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(BlueprintPath, LoadError);
		if (!Blueprint)
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
		}

		USCS_Node* WidgetComponentNode = FindWidgetComponentNode(Blueprint, ComponentName);
		if (ComponentName.IsEmpty() && CountWidgetComponentNodes(Blueprint) > 1)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"),
				TEXT("Multiple WidgetComponents found in Blueprint. Specify 'component_name'"));
		}

		if (!WidgetComponentNode || !WidgetComponentNode->ComponentTemplate)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"),
				ComponentName.IsEmpty()
					? TEXT("WidgetComponent template not found. Specify 'component_name' if the Blueprint contains multiple components.")
					: FString::Printf(TEXT("WidgetComponent '%s' was not found in Blueprint"), *ComponentName));
		}

		WidgetComponent = Cast<UWidgetComponent>(WidgetComponentNode->ComponentTemplate);
		if (!WidgetComponent)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"),
				TEXT("Resolved component is not a WidgetComponent"));
		}

		bIsBlueprintTarget = true;
	}
	else
	{
		bool bIsPIE = false;
		UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType, bIsPIE);
		if (!World)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"),
				FString::Printf(TEXT("World '%s' is not available"), *WorldType));
		}

		Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
		if (!Actor)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"),
				FString::Printf(TEXT("Actor '%s' was not found"), *ActorName));
		}

		FString FindComponentError;
		WidgetComponent = FindWidgetComponentOnActor(Actor, ComponentName, FindComponentError);
		if (!WidgetComponent)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), FindComponentError);
		}
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
		WidgetComponent->Modify();
		if (Blueprint)
		{
			Blueprint->Modify();
		}
		if (Actor)
		{
			Actor->Modify();
		}
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

		if (ActionName == TEXT("set_widget_class"))
		{
			FString WidgetClassName;
			if (!(*OperationObject)->TryGetStringField(TEXT("widget_class"), WidgetClassName))
			{
				OperationError = TEXT("'widget_class' is required for set_widget_class");
			}
			else
			{
				OperationResult->SetStringField(TEXT("widget_class"), WidgetClassName);
				bOperationSuccess = ApplyWidgetComponentProperty(
					WidgetComponent,
					TEXT("widget_class"),
					MakeShareable(new FJsonValueString(WidgetClassName)),
					!bDryRun,
					OperationError);
				bOperationChanged = bOperationSuccess;
			}
		}
		else if (ActionName == TEXT("set_property"))
		{
			FString PropertyPath;
			TSharedPtr<FJsonValue> Value = nullptr;
			if (!(*OperationObject)->TryGetStringField(TEXT("property"), PropertyPath))
			{
				OperationError = TEXT("'property' is required for set_property");
			}
			else
			{
				Value = (*OperationObject)->TryGetField(TEXT("value"));
				if (!Value.IsValid())
				{
					OperationError = TEXT("'value' is required for set_property");
				}
			}

			if (OperationError.IsEmpty())
			{
				OperationResult->SetStringField(TEXT("property"), PropertyPath);
				bOperationSuccess = ApplyWidgetComponentProperty(WidgetComponent, PropertyPath, Value, !bDryRun, OperationError);
				bOperationChanged = bOperationSuccess;
			}
		}
		else if (ActionName == TEXT("set_properties"))
		{
			const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
			if (!(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !(*PropertiesObject).IsValid())
			{
				OperationError = TEXT("'properties' object is required for set_properties");
			}
			else
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertiesObject)->Values)
				{
					if (!ApplyWidgetComponentProperty(WidgetComponent, Pair.Key, Pair.Value, !bDryRun, OperationError))
					{
						break;
					}
				}

				bOperationSuccess = OperationError.IsEmpty();
				bOperationChanged = bOperationSuccess;
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
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), OperationError);
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
		FMcpAssetModifier::MarkPackageDirty(WidgetComponent);
		if (Blueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	bool bCompileAttempted = false;
	bool bCompileSuccess = true;
	FString CompileError;
	if (!bDryRun && bAnyChanged && Blueprint)
	{
		const bool bShouldCompile = (CompilePolicy == TEXT("always")) ||
			(CompilePolicy == TEXT("if_needed"));
		if (bShouldCompile)
		{
			bCompileAttempted = true;
			FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);
			bCompileSuccess = FMcpAssetModifier::CompileBlueprint(Blueprint, CompileError);
			if (!bCompileSuccess)
			{
				TSharedPtr<FJsonObject> DiagnosticObject = MakeShareable(new FJsonObject);
				DiagnosticObject->SetStringField(TEXT("severity"), TEXT("error"));
				DiagnosticObject->SetStringField(TEXT("code"), TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"));
				DiagnosticObject->SetStringField(TEXT("message"), CompileError);
				DiagnosticObject->SetStringField(TEXT("asset_path"), BlueprintPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(DiagnosticObject)));
				bAnyFailed = true;
			}
		}
	}
	else if (!Blueprint && CompilePolicy != TEXT("never"))
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("Compile is only supported for blueprint targets; compile was skipped for actor instances."))));
	}

	if (!bDryRun && bSave && !bAnyFailed)
	{
		if (Blueprint)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError) && !SaveError.IsEmpty())
			{
				WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Save failed: %s"), *SaveError))));
			}
		}
		else
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("Save is only supported for blueprint targets; actor-instance changes remain dirty in the current world."))));
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
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("target_type"), bIsBlueprintTarget ? TEXT("blueprint") : TEXT("actor"));
	if (bIsBlueprintTarget)
	{
		Response->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	}
	else
	{
		Response->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Response->SetStringField(TEXT("world"), WorldType);
	}
	Response->SetStringField(TEXT("component_name"), WidgetComponent->GetName());

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	if (bIsBlueprintTarget)
	{
		ModifiedAssets.Add(MakeShareable(new FJsonValueString(BlueprintPath)));
	}
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
