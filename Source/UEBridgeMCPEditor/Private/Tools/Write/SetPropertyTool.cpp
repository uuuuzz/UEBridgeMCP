// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Write/SetPropertyTool.h"
#include "Utils/McpAssetModifier.h"
#include "UEBridgeMCPEditor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

FString USetPropertyTool::GetToolDescription() const
{
	return TEXT("Set any property on any asset using UE reflection. Supports nested paths (e.g., 'Stats.MaxHealth'), "
		"array indices (e.g., 'Items[0]'), TArray, TMap (as JSON object), TSet (as JSON array), "
		"object references (as asset paths), structs, and vectors/rotators/colors. "
		"For Blueprint components, use 'component_name' to target a specific component's properties. "
		"Use 'clear_override' to revert a component property to its default value.");
}

TMap<FString, FMcpSchemaProperty> USetPropertyTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path (e.g., '/Game/Blueprints/BP_Player')");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FMcpSchemaProperty PropertyPath;
	PropertyPath.Type = TEXT("string");
	PropertyPath.Description = TEXT("Dot-separated property path (e.g., 'Health', 'Stats.MaxHealth', 'Items[0].Value')");
	PropertyPath.bRequired = true;
	Schema.Add(TEXT("property_path"), PropertyPath);

	FMcpSchemaProperty Value;
	Value.Type = TEXT("any");
	Value.Description = TEXT("Value to set (number, string, boolean, array, or object depending on property type). Not required if clear_override is true.");
	Value.bRequired = false;
	Schema.Add(TEXT("value"), Value);

	FMcpSchemaProperty ComponentName;
	ComponentName.Type = TEXT("string");
	ComponentName.Description = TEXT("Name of the component to target (for Blueprint component overrides). If specified, property_path is relative to the component.");
	ComponentName.bRequired = false;
	Schema.Add(TEXT("component_name"), ComponentName);

	FMcpSchemaProperty ClearOverride;
	ClearOverride.Type = TEXT("boolean");
	ClearOverride.Description = TEXT("If true, clears the override and reverts to the default value (component class default for SCS, parent value for inherited). Mutually exclusive with 'value'.");
	ClearOverride.bRequired = false;
	Schema.Add(TEXT("clear_override"), ClearOverride);

	return Schema;
}

TArray<FString> USetPropertyTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("property_path") };
}

FMcpToolResult USetPropertyTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	// Get parameters
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString PropertyPath = GetStringArgOrDefault(Arguments, TEXT("property_path"));
	FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"), TEXT(""));
	bool bClearOverride = GetBoolArgOrDefault(Arguments, TEXT("clear_override"), false);

	if (AssetPath.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("asset_path is required"));
	}

	if (PropertyPath.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("property_path is required"));
	}

	// Get the value (can be any JSON type) - not required if clear_override is true
	TSharedPtr<FJsonValue> Value = Arguments->TryGetField(TEXT("value"));
	if (!Value.IsValid() && !bClearOverride)
	{
		return FMcpToolResult::Error(TEXT("Either 'value' or 'clear_override' must be provided"));
	}

	if (Value.IsValid() && bClearOverride)
	{
		return FMcpToolResult::Error(TEXT("Cannot specify both 'value' and 'clear_override'"));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("set-property: %s.%s (component=%s, clear=%s)"),
		*AssetPath, *PropertyPath, *ComponentName, bClearOverride ? TEXT("true") : TEXT("false"));

	// Load the asset
	FString LoadError;
	UObject* Object = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Object)
	{
		return FMcpToolResult::Error(LoadError);
	}

	// For Blueprints, we need to handle component targeting
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	UObject* TargetObject = Object;
	UActorComponent* ComponentTemplate = nullptr;
	UActorComponent* DefaultComponent = nullptr;  // For clear_override comparison
	bool bIsInheritedComponent = false;

	if (Blueprint)
	{
		if (!Blueprint->GeneratedClass)
		{
			return FMcpToolResult::Error(TEXT("Blueprint has no generated class - compile it first"));
		}

		if (!ComponentName.IsEmpty())
		{
			// Component targeting mode - find the component template
			bool bFoundComponent = false;

			// Try SCS first (components added in this Blueprint)
			if (Blueprint->SimpleConstructionScript)
			{
				TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
				for (USCS_Node* Node : AllNodes)
				{
					if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString() == ComponentName)
					{
						ComponentTemplate = Node->ComponentTemplate;
						TargetObject = ComponentTemplate;
						// For SCS components, the default is the component class CDO
						DefaultComponent = ComponentTemplate->GetClass()->GetDefaultObject<UActorComponent>();
						bFoundComponent = true;
						bIsInheritedComponent = false;
						break;
					}
				}
			}

			// Try InheritableComponentHandler (inherited component overrides)
			if (!bFoundComponent)
			{
			UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(true);
				if (ICH)
				{
					TArray<UActorComponent*> OverrideTemplates;
					ICH->GetAllTemplates(OverrideTemplates);

					for (UActorComponent* ExistingTemplate : OverrideTemplates)
					{
						if (!ExistingTemplate) continue;
						FComponentKey Key = ICH->FindKey(ExistingTemplate);
						FString KeyName = Key.GetSCSVariableName().ToString();

						if (KeyName == ComponentName || (ExistingTemplate && ExistingTemplate->GetName() == ComponentName))
						{
							ComponentTemplate = ExistingTemplate;
							TargetObject = ComponentTemplate;
							bIsInheritedComponent = true;
							bFoundComponent = true;

							// For inherited components, find the parent component for comparison
							UClass* ParentClass = Blueprint->ParentClass;
							if (ParentClass)
							{
								if (AActor* ParentCDO = Cast<AActor>(ParentClass->GetDefaultObject()))
								{
									TArray<UActorComponent*> ParentComponents;
									ParentCDO->GetComponents(ParentComponents);
									for (UActorComponent* Comp : ParentComponents)
									{
										if (Comp && Comp->GetName() == ComponentTemplate->GetName())
										{
											DefaultComponent = Comp;
											break;
										}
									}
								}
							}
							break;
						}
					}
				}

				// If not found in ICH, check three fallback sources for inherited components:
				// 1. An existing SCS node in a parent Blueprint (needs ICH override via FComponentKey)
				// 2. A native subobject from a C++ parent class (e.g. ACharacter's CharacterMesh0/CharMoveComp)
				//    - directly edit the Blueprint CDO's default subobject in that case
				// Both are handled by first resolving the "parent template" and its FComponentKey (if any).
				if (!bFoundComponent)
				{
					UClass* ParentClass = Blueprint->ParentClass;
					if (ParentClass)
					{
						// --- Path A: parent Blueprint's SCS node (needs ICH override) ---
						UActorComponent* ParentSCSTemplate = nullptr;
						FComponentKey ResolvedKey;
						{
							UClass* WalkClass = ParentClass;
							while (WalkClass && !ParentSCSTemplate)
							{
								UBlueprintGeneratedClass* ParentBPGC = Cast<UBlueprintGeneratedClass>(WalkClass);
								if (!ParentBPGC) { break; }
								UBlueprint* ParentBP = Cast<UBlueprint>(ParentBPGC->ClassGeneratedBy);
								if (ParentBP && ParentBP->SimpleConstructionScript)
								{
									TArray<USCS_Node*> ParentNodes = ParentBP->SimpleConstructionScript->GetAllNodes();
									for (USCS_Node* PNode : ParentNodes)
									{
										if (PNode && PNode->ComponentTemplate && PNode->GetVariableName().ToString() == ComponentName)
										{
											ParentSCSTemplate = PNode->ComponentTemplate;
											ResolvedKey = FComponentKey(PNode);
											break;
										}
									}
								}
								WalkClass = WalkClass->GetSuperClass();
							}
						}

						if (ParentSCSTemplate && ResolvedKey.IsValid())
						{
							UInheritableComponentHandler* ICH2 = Blueprint->GetInheritableComponentHandler(true);
							if (ICH2)
							{
								FScopedTransaction OverrideTransaction(FText::Format(
									NSLOCTEXT("UEBridgeMCP", "CreateCompOverride", "Create override for '{0}'"),
									FText::FromString(ComponentName)));
								ICH2->Modify();
								UActorComponent* NewOverride = ICH2->CreateOverridenComponentTemplate(ResolvedKey);
								if (NewOverride)
								{
									ComponentTemplate = NewOverride;
									TargetObject = ComponentTemplate;
									DefaultComponent = ParentSCSTemplate; // revert target == parent value
									bIsInheritedComponent = true;
									bFoundComponent = true;
									UE_LOG(LogUEBridgeMCP, Log,
										TEXT("set-property: Created ICH override for inherited component '%s' (parent SCS)"),
										*ComponentName);
								}
							}
						}

						// --- Path B: native subobject on a C++ parent (e.g. ACharacter's CharacterMesh0/CharMoveComp) ---
						if (!bFoundComponent)
						{
							UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
							if (CDO)
							{
								UActorComponent* CDOSubobject = Cast<UActorComponent>(
									CDO->GetDefaultSubobjectByName(FName(*ComponentName)));
								if (CDOSubobject)
								{
									// Native components are authored directly on the Blueprint CDO.
									// Writing to this instance is what the editor does when you tweak an
									// inherited component's default in the details panel.
									ComponentTemplate = CDOSubobject;
									TargetObject = CDOSubobject;
									// For clear_override, fall back to the parent class CDO's subobject
									if (AActor* ParentCDOActor = Cast<AActor>(ParentClass->GetDefaultObject()))
									{
										TArray<UActorComponent*> ParentComponents;
										ParentCDOActor->GetComponents(ParentComponents);
										for (UActorComponent* Comp : ParentComponents)
										{
											if (Comp && Comp->GetName() == ComponentName)
											{
												DefaultComponent = Comp;
												break;
											}
										}
									}
									bIsInheritedComponent = true;
									bFoundComponent = true;
									UE_LOG(LogUEBridgeMCP, Log,
										TEXT("set-property: Editing native inherited component '%s' on Blueprint CDO"),
										*ComponentName);
								}
							}
						}
					}
				}
			}

			if (!bFoundComponent)
			{
				return FMcpToolResult::Error(FString::Printf(TEXT("UEBMCP_COMPONENT_NOT_FOUND: Component '%s' not found in Blueprint (searched SCS, ICH, parent SCS, native subobjects)"), *ComponentName));
			}
		}
		else
		{
			// No component specified - target the CDO
			TargetObject = Blueprint->GeneratedClass->GetDefaultObject();
			if (!TargetObject)
			{
				return FMcpToolResult::Error(TEXT("Failed to get Blueprint CDO"));
			}
		}
	}

	// Find the property
	FProperty* Property = nullptr;
	void* Container = nullptr;
	FString FindError;

	if (!FMcpAssetModifier::FindPropertyByPath(TargetObject, PropertyPath, Property, Container, FindError))
	{
		return FMcpToolResult::Error(FindError);
	}

	// Begin transaction for undo support
	FString TransactionDesc = bClearOverride
		? FString::Printf(TEXT("Clear override %s.%s"), *AssetPath, *PropertyPath)
		: FString::Printf(TEXT("Set %s.%s"), *AssetPath, *PropertyPath);

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(
		FText::FromString(TransactionDesc));

	// Mark objects as modified
	FMcpAssetModifier::MarkModified(TargetObject);
	if (Blueprint)
	{
		FMcpAssetModifier::MarkModified(Blueprint);
	}

	if (bClearOverride)
	{
		// Clear the override - copy default value to the template
		if (!DefaultComponent)
		{
			// Fall back to class CDO
			DefaultComponent = TargetObject->GetClass()->GetDefaultObject<UActorComponent>();
		}

		if (DefaultComponent)
		{
			void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(DefaultComponent);
			void* TargetValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

			if (DefaultValuePtr && TargetValuePtr)
			{
				Property->CopyCompleteValue(TargetValuePtr, DefaultValuePtr);
				UE_LOG(LogUEBridgeMCP, Log, TEXT("set-property: Cleared override for %s.%s"), *AssetPath, *PropertyPath);
			}
			else
			{
				return FMcpToolResult::Error(TEXT("Failed to get property value pointers for clear operation"));
			}
		}
		else
		{
			return FMcpToolResult::Error(TEXT("Cannot determine default value for clear_override - no default component available"));
		}
	}
	else
	{
		// Set the property value
		FString SetError;
		if (!FMcpAssetModifier::SetPropertyFromJson(Property, Container, Value, SetError))
		{
			return FMcpToolResult::Error(SetError);
		}
	}

	// Mark package as dirty
	FMcpAssetModifier::MarkPackageDirty(Object);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset"), AssetPath);
	Result->SetStringField(TEXT("property"), PropertyPath);

	if (!ComponentName.IsEmpty())
	{
		Result->SetStringField(TEXT("component"), ComponentName);
		Result->SetBoolField(TEXT("is_inherited_component"), bIsInheritedComponent);
	}

	if (bClearOverride)
	{
		Result->SetBoolField(TEXT("override_cleared"), true);
	}

	Result->SetBoolField(TEXT("needs_save"), true);

	if (Blueprint)
	{
		Result->SetBoolField(TEXT("needs_compile"), true);
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("set-property: Successfully %s %s.%s%s"),
		bClearOverride ? TEXT("cleared") : TEXT("set"),
		*AssetPath, *PropertyPath,
		ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (component: %s)"), *ComponentName));

	return FMcpToolResult::Json(Result);
}
