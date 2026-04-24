// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/EditBlueprintComponentsTool.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/SceneComponent.h"
#include "Serialization/JsonSerializer.h"

FString UEditBlueprintComponentsTool::GetToolDescription() const
{
	return TEXT("Edit Blueprint component tree (SCS): add, remove, rename, attach, set defaults, and set root component. "
		"Accepts a batched 'actions' array. Supports compile, save, dry_run, and rollback_on_error options.");
}

TMap<FString, FMcpSchemaProperty> UEditBlueprintComponentsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> TransformSchema = MakeShared<FMcpSchemaProperty>();
	TransformSchema->Type = TEXT("object");
	TransformSchema->Description = TEXT("Relative transform descriptor for SceneComponent defaults");
	TransformSchema->Properties.Add(TEXT("location"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Relative location [X,Y,Z]"), TEXT("number"))));
	TransformSchema->Properties.Add(TEXT("rotation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Relative rotation [Pitch,Yaw,Roll]"), TEXT("number"))));
	TransformSchema->Properties.Add(TEXT("scale"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Relative scale [X,Y,Z]"), TEXT("number"))));

	TSharedPtr<FJsonObject> PropertiesRawSchema = MakeShareable(new FJsonObject);
	PropertiesRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	PropertiesRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> PropertiesSchema = MakeShared<FMcpSchemaProperty>();
	PropertiesSchema->Description = TEXT("Component default property overrides");
	PropertiesSchema->RawSchema = PropertiesRawSchema;

	TSharedPtr<FMcpSchemaProperty> ActionItemSchema = MakeShared<FMcpSchemaProperty>();
	ActionItemSchema->Type = TEXT("object");
	ActionItemSchema->Description = TEXT("Blueprint component tree edit action");
	ActionItemSchema->NestedRequired = {TEXT("action")};
	ActionItemSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Component edit action"),
		{TEXT("add_component"), TEXT("remove_component"), TEXT("rename_component"), TEXT("attach_component"), TEXT("reparent_component"), TEXT("set_component_defaults"), TEXT("set_root_component")},
		true)));
	ActionItemSchema->Properties.Add(TEXT("component_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target component name"))));
	ActionItemSchema->Properties.Add(TEXT("component_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component class path for add_component"))));
	ActionItemSchema->Properties.Add(TEXT("parent_component"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Parent component name for attach/reparent"))));
	ActionItemSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New component name for rename_component"))));
	ActionItemSchema->Properties.Add(TEXT("attach_socket"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional attach socket name"))));
	ActionItemSchema->Properties.Add(TEXT("transform"), TransformSchema);
	ActionItemSchema->Properties.Add(TEXT("defaults"), PropertiesSchema);
	ActionItemSchema->Properties.Add(TEXT("properties"), PropertiesSchema);
	ActionItemSchema->Properties.Add(TEXT("make_root"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the new component should become root during add_component"))));
	ActionItemSchema->Properties.Add(TEXT("clear_overrides"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether overridden component defaults should be cleared before applying properties"))));

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Array of Blueprint component tree edit actions with nested transform and property descriptors.");
	ActionsSchema.bRequired = true;
	ActionsSchema.Items = ActionItemSchema;
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy: 'never', 'if_needed', 'always'"),
		{TEXT("never"), TEXT("if_needed"), TEXT("always")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save asset after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transaction label")));

	return Schema;
}

TArray<FString> UEditBlueprintComponentsTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("actions")};
}

FMcpToolResult UEditBlueprintComponentsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("never"));
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Blueprint Components"));

	FString LoadError;
	UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"),
			TEXT("Blueprint does not have a SimpleConstructionScript (not an Actor Blueprint?)"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bStructuralChange = false;

	for (int32 i = 0; i < ActionsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& ActionValue = (*ActionsArray)[i];
		const TSharedPtr<FJsonObject>* ActionObj = nullptr;
		if (!ActionValue.IsValid() || !ActionValue->TryGetObject(ActionObj) || !(*ActionObj).IsValid())
		{
			if (bRollbackOnError)
			{
				Transaction.Reset();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Action at index %d is not a valid object"), i));
			}
			continue;
		}

		TSharedPtr<FJsonObject> ActionResult = MakeShareable(new FJsonObject);
		FString ActionError;

		bool bSuccess = false;
		if (!bDryRun)
		{
			bSuccess = ExecuteAction(Blueprint, *ActionObj, i, ActionResult, ActionError);
		}
		else
		{
			FString ActionName;
			if ((*ActionObj)->TryGetStringField(TEXT("action"), ActionName))
			{
				ActionResult->SetStringField(TEXT("action"), ActionName);
				bSuccess = true;
			}
			else
			{
				ActionError = TEXT("Missing 'action' field");
			}
		}

		ActionResult->SetNumberField(TEXT("index"), i);
		ActionResult->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			ActionResult->SetStringField(TEXT("error"), ActionError);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
			if (bRollbackOnError && !bDryRun)
			{
				Transaction.Reset();
				TSharedPtr<FJsonObject> ErrorDetails = MakeShareable(new FJsonObject);
				ErrorDetails->SetStringField(TEXT("asset_path"), AssetPath);
				ErrorDetails->SetNumberField(TEXT("failed_action_index"), i);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), ActionError, ErrorDetails);
			}
		}
		else
		{
			bStructuralChange = true;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
	}

	TSharedPtr<FJsonObject> CompileResult = MakeShareable(new FJsonObject);
	bool bCompileAttempted = false;
	bool bCompileSuccess = true;
	FString CompileError;

	if (!bDryRun && bStructuralChange)
	{
		const bool bShouldCompile = (CompilePolicy == TEXT("always")) ||
			(CompilePolicy == TEXT("if_needed") && bStructuralChange);

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
				DiagnosticObject->SetStringField(TEXT("asset_path"), AssetPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(DiagnosticObject)));

				TSharedPtr<FJsonObject> PartialResultObject = MakeShareable(new FJsonObject);
				PartialResultObject->SetStringField(TEXT("stage"), TEXT("compile"));
				PartialResultObject->SetBoolField(TEXT("success"), false);
				PartialResultObject->SetStringField(TEXT("error"), CompileError);
				PartialResultObject->SetStringField(TEXT("asset_path"), AssetPath);
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(PartialResultObject)));
				bAnyFailed = true;
			}
		}
	}

	CompileResult->SetBoolField(TEXT("attempted"), bCompileAttempted);
	CompileResult->SetBoolField(TEXT("success"), bCompileSuccess);
	CompileResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);

	if (!bDryRun && bSave && !bAnyFailed)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError) && !SaveError.IsEmpty())
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Save failed: %s"), *SaveError))));
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("edit-blueprint-components"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	ModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetObjectField(TEXT("compile"), CompileResult);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

bool UEditBlueprintComponentsTool::ExecuteAction(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	int32 Index,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActionName;
	if (!Action->TryGetStringField(TEXT("action"), ActionName))
	{
		OutError = TEXT("Missing 'action' field");
		return false;
	}

	OutResult->SetStringField(TEXT("action"), ActionName);

	if (ActionName == TEXT("add_component")) { return AddComponent(Blueprint, Action, OutResult, OutError); }
	if (ActionName == TEXT("remove_component")) { return RemoveComponent(Blueprint, Action, OutResult, OutError); }
	if (ActionName == TEXT("rename_component")) { return RenameComponent(Blueprint, Action, OutResult, OutError); }
	if (ActionName == TEXT("attach_component")) { return AttachComponent(Blueprint, Action, OutResult, OutError); }
	if (ActionName == TEXT("reparent_component")) { return AttachComponent(Blueprint, Action, OutResult, OutError); }
	if (ActionName == TEXT("set_component_defaults")) { return SetComponentDefaults(Blueprint, Action, OutResult, OutError); }
	if (ActionName == TEXT("set_root_component")) { return SetRootComponent(Blueprint, Action, OutResult, OutError); }

	OutError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
	return false;
}

USCS_Node* UEditBlueprintComponentsTool::FindSCSNode(UBlueprint* Blueprint, const FString& ComponentName)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return nullptr;
	}

	const TArray<USCS_Node*>& AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
	for (USCS_Node* Node : AllNodes)
	{
		if (Node && Node->GetVariableName().ToString() == ComponentName)
		{
			return Node;
		}
	}
	return nullptr;
}

bool UEditBlueprintComponentsTool::AddComponent(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ComponentName, ComponentClassPath;
	if (!Action->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		OutError = TEXT("'component_name' is required");
		return false;
	}
	if (!Action->TryGetStringField(TEXT("component_class"), ComponentClassPath))
	{
		OutError = TEXT("'component_class' is required");
		return false;
	}

	OutResult->SetStringField(TEXT("component_name"), ComponentName);

	// Resolve the component class.
	UClass* ComponentClass = FindObject<UClass>(nullptr, *ComponentClassPath);
	if (!ComponentClass)
	{
		ComponentClass = LoadObject<UClass>(nullptr, *ComponentClassPath);
	}
	if (!ComponentClass)
	{
		OutError = FString::Printf(TEXT("Component class not found: '%s'"), *ComponentClassPath);
		return false;
	}

	if (!ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		OutError = FString::Printf(TEXT("'%s' is not a UActorComponent subclass"), *ComponentClassPath);
		return false;
	}

	// Reject duplicate component names.
	if (FindSCSNode(Blueprint, ComponentName))
	{
		OutError = FString::Printf(TEXT("Component '%s' already exists"), *ComponentName);
		return false;
	}

	// Create the SCS node.
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ComponentName));
	if (!NewNode)
	{
		OutError = TEXT("Failed to create SCS node");
		return false;
	}

	// Attach the node to the requested parent, or to the root if no valid parent was found.
	FString ParentName;
	if (Action->TryGetStringField(TEXT("parent_component"), ParentName) && !ParentName.IsEmpty())
	{
		USCS_Node* ParentNode = FindSCSNode(Blueprint, ParentName);
		if (ParentNode)
		{
			ParentNode->AddChildNode(NewNode);
		}
		else
		{
			Blueprint->SimpleConstructionScript->AddNode(NewNode);
		}
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	// Apply transform defaults for scene components.
	if (ComponentClass->IsChildOf(USceneComponent::StaticClass()))
	{
		const TSharedPtr<FJsonObject>* TransformObj = nullptr;
		if (Action->TryGetObjectField(TEXT("transform"), TransformObj) && TransformObj && (*TransformObj).IsValid())
		{
			USceneComponent* Template = Cast<USceneComponent>(NewNode->ComponentTemplate);
			if (Template)
			{
				const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
				if ((*TransformObj)->TryGetArrayField(TEXT("location"), LocArray) && LocArray && LocArray->Num() >= 3)
				{
					Template->SetRelativeLocation(FVector(
						(*LocArray)[0]->AsNumber(),
						(*LocArray)[1]->AsNumber(),
						(*LocArray)[2]->AsNumber()));
				}
				const TArray<TSharedPtr<FJsonValue>>* RotArray = nullptr;
				if ((*TransformObj)->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray && RotArray->Num() >= 3)
				{
					Template->SetRelativeRotation(FRotator(
						(*RotArray)[0]->AsNumber(),
						(*RotArray)[1]->AsNumber(),
						(*RotArray)[2]->AsNumber()));
				}
				const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;
				if ((*TransformObj)->TryGetArrayField(TEXT("scale"), ScaleArray) && ScaleArray && ScaleArray->Num() >= 3)
				{
					Template->SetRelativeScale3D(FVector(
						(*ScaleArray)[0]->AsNumber(),
						(*ScaleArray)[1]->AsNumber(),
						(*ScaleArray)[2]->AsNumber()));
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintComponentsTool::RemoveComponent(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ComponentName;
	if (!Action->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		OutError = TEXT("'component_name' is required");
		return false;
	}

	OutResult->SetStringField(TEXT("component_name"), ComponentName);

	USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName);
		return false;
	}

	Blueprint->SimpleConstructionScript->RemoveNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintComponentsTool::RenameComponent(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ComponentName, NewName;
	if (!Action->TryGetStringField(TEXT("component_name"), ComponentName) ||
		!Action->TryGetStringField(TEXT("new_name"), NewName))
	{
		OutError = TEXT("'component_name' and 'new_name' are required");
		return false;
	}

	OutResult->SetStringField(TEXT("component_name"), ComponentName);
	OutResult->SetStringField(TEXT("new_name"), NewName);

	USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName);
		return false;
	}

	FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, Node, FName(*NewName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintComponentsTool::AttachComponent(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ComponentName, ParentName;
	if (!Action->TryGetStringField(TEXT("component_name"), ComponentName) ||
		!Action->TryGetStringField(TEXT("parent_component"), ParentName))
	{
		OutError = TEXT("'component_name' and 'parent_component' are required");
		return false;
	}

	OutResult->SetStringField(TEXT("component_name"), ComponentName);
	OutResult->SetStringField(TEXT("parent_component"), ParentName);

	USCS_Node* ChildNode = FindSCSNode(Blueprint, ComponentName);
	USCS_Node* ParentNode = FindSCSNode(Blueprint, ParentName);

	if (!ChildNode)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName);
		return false;
	}
	if (!ParentNode)
	{
		OutError = FString::Printf(TEXT("Parent component '%s' not found"), *ParentName);
		return false;
	}

	// Detach the node from its current parent.
	Blueprint->SimpleConstructionScript->RemoveNode(ChildNode, false);
	// Reattach the node under the requested parent.
	ParentNode->AddChildNode(ChildNode);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintComponentsTool::SetComponentDefaults(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ComponentName;
	if (!Action->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		OutError = TEXT("'component_name' is required");
		return false;
	}

	OutResult->SetStringField(TEXT("component_name"), ComponentName);

	USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
	if (!Node || !Node->ComponentTemplate)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found or has no template"), *ComponentName);
		return false;
	}

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (!Action->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj || !(*PropsObj).IsValid())
	{
		OutError = TEXT("'properties' object is required for set_component_defaults");
		return false;
	}

	// Reuse McpAssetModifier property application logic.
	for (const auto& Pair : (*PropsObj)->Values)
	{
		FProperty* Prop = nullptr;
		void* Container = nullptr;
		FString PropError;

		if (FMcpAssetModifier::FindPropertyByPath(Node->ComponentTemplate, Pair.Key, Prop, Container, PropError))
		{
			FMcpAssetModifier::SetPropertyFromJson(Prop, Container, Pair.Value, PropError);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

bool UEditBlueprintComponentsTool::SetRootComponent(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ComponentName;
	if (!Action->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		OutError = TEXT("'component_name' is required");
		return false;
	}

	OutResult->SetStringField(TEXT("component_name"), ComponentName);

	USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName);
		return false;
	}

	if (!Node->ComponentTemplate || !Node->ComponentTemplate->IsA<USceneComponent>())
	{
		OutError = TEXT("Only SceneComponent subclasses can be set as root");
		return false;
	}

	// Promote the node to the root by caching the old root nodes first, then removing and reattaching nodes.
	// The cache is required because RemoveNode mutates the array returned by GetRootNodes().
	TArray<USCS_Node*> OldRootNodes = Blueprint->SimpleConstructionScript->GetRootNodes();
	OldRootNodes.Remove(Node); // Exclude the node itself if it is already one of the roots.

	Blueprint->SimpleConstructionScript->RemoveNode(Node, false);

	// Reattach the previous root nodes under the new root.
	for (USCS_Node* OldRoot : OldRootNodes)
	{
		if (OldRoot)
		{
			Blueprint->SimpleConstructionScript->RemoveNode(OldRoot, false);
			Node->AddChildNode(OldRoot);
		}
	}

	Blueprint->SimpleConstructionScript->AddNode(Node);
	
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}
