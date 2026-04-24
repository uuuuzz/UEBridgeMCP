// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/ApplyMaterialTool.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor.h"
#include "Serialization/JsonSerializer.h"

namespace ApplyMaterialToolPrivate
{
	UMeshComponent* FindMeshComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		if (!ComponentName.IsEmpty())
		{
			return Cast<UMeshComponent>(FMcpAssetModifier::FindComponentByName(Actor, ComponentName));
		}

		return Actor->FindComponentByClass<UMeshComponent>();
	}

	UMeshComponent* FindBlueprintMeshComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}

		const TArray<USCS_Node*>& AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* Node : AllNodes)
		{
			if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Cast<UMeshComponent>(Node->ComponentTemplate);
			}
		}

		return nullptr;
	}

	bool ResolveSlotIndex(
		UMeshComponent* MeshComponent,
		const FString& SlotName,
		bool bHasSlotIndex,
		int32 InSlotIndex,
		int32& OutSlotIndex,
		FString& OutError)
	{
		if (!MeshComponent)
		{
			OutError = TEXT("Mesh component is null");
			return false;
		}

		if (!SlotName.IsEmpty())
		{
			const int32 SlotIndexFromName = MeshComponent->GetMaterialIndex(FName(*SlotName));
			if (SlotIndexFromName < 0)
			{
				OutError = FString::Printf(TEXT("Material slot '%s' was not found on component '%s'"), *SlotName, *MeshComponent->GetName());
				return false;
			}

			if (bHasSlotIndex && InSlotIndex != SlotIndexFromName)
			{
				OutError = FString::Printf(TEXT("'slot_name' resolved to index %d but explicit 'slot_index' was %d"), SlotIndexFromName, InSlotIndex);
				return false;
			}

			OutSlotIndex = SlotIndexFromName;
			return true;
		}

		OutSlotIndex = bHasSlotIndex ? InSlotIndex : 0;
		if (OutSlotIndex < 0 || OutSlotIndex >= MeshComponent->GetNumMaterials())
		{
			OutError = FString::Printf(TEXT("Slot index %d out of range (0-%d)"), OutSlotIndex, FMath::Max(0, MeshComponent->GetNumMaterials() - 1));
			return false;
		}

		return true;
	}

	bool ApplyEditorParameterOverrides(
		UMaterialInterface* MaterialInterface,
		const TSharedPtr<FJsonObject>& ParameterOverrides,
		FString& OutError)
	{
		if (!MaterialInterface || !ParameterOverrides.IsValid())
		{
			return true;
		}

		UMaterial* Material = Cast<UMaterial>(MaterialInterface);
		UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(MaterialInterface);
		if (!Material && !MaterialInstance)
		{
			OutError = TEXT("Parameter overrides without create_dynamic_instance require a UMaterial or UMaterialInstanceConstant asset");
			return false;
		}

		FMcpAssetModifier::MarkModified(MaterialInterface);

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ParameterOverrides->Values)
		{
			const FName ParameterName(*Pair.Key);
			const TSharedPtr<FJsonValue>& Value = Pair.Value;
			if (!Value.IsValid())
			{
				continue;
			}

			if (Value->Type == EJson::Number || Value->Type == EJson::Boolean)
			{
				const float ScalarValue = Value->Type == EJson::Boolean ? (Value->AsBool() ? 1.0f : 0.0f) : static_cast<float>(Value->AsNumber());
				if (Material)
				{
					Material->SetScalarParameterValueEditorOnly(ParameterName, ScalarValue);
				}
				else
				{
					MaterialInstance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ParameterName), ScalarValue);
				}
			}
			else if (Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& ArrayValue = Value->AsArray();
				if (ArrayValue.Num() < 3)
				{
					OutError = FString::Printf(TEXT("Parameter '%s' requires at least 3 numbers for vector values"), *Pair.Key);
					return false;
				}

				const FLinearColor LinearColor(
					static_cast<float>(ArrayValue[0]->AsNumber()),
					static_cast<float>(ArrayValue[1]->AsNumber()),
					static_cast<float>(ArrayValue[2]->AsNumber()),
					ArrayValue.Num() >= 4 ? static_cast<float>(ArrayValue[3]->AsNumber()) : 1.0f);

				if (Material)
				{
					Material->SetVectorParameterValueEditorOnly(ParameterName, LinearColor);
				}
				else
				{
					MaterialInstance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ParameterName), LinearColor);
				}
			}
			else if (Value->Type == EJson::String)
			{
				FString TextureLoadError;
				UTexture* Texture = FMcpAssetModifier::LoadAssetByPath<UTexture>(Value->AsString(), TextureLoadError);
				if (!Texture)
				{
					OutError = TextureLoadError;
					return false;
				}

				if (Material)
				{
					Material->SetTextureParameterValueEditorOnly(ParameterName, Texture);
				}
				else
				{
					MaterialInstance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParameterName), Texture);
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("Unsupported parameter override value type for '%s'"), *Pair.Key);
				return false;
			}
		}

		MaterialInterface->PostEditChange();
		FMcpAssetModifier::MarkPackageDirty(MaterialInterface);
		return true;
	}

	bool ApplyDynamicParameterOverrides(
		UMaterialInstanceDynamic* MaterialInstance,
		const TSharedPtr<FJsonObject>& ParameterOverrides,
		FString& OutError)
	{
		if (!MaterialInstance || !ParameterOverrides.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ParameterOverrides->Values)
		{
			const FName ParameterName(*Pair.Key);
			const TSharedPtr<FJsonValue>& Value = Pair.Value;
			if (!Value.IsValid())
			{
				continue;
			}

			if (Value->Type == EJson::Number || Value->Type == EJson::Boolean)
			{
				const float ScalarValue = Value->Type == EJson::Boolean ? (Value->AsBool() ? 1.0f : 0.0f) : static_cast<float>(Value->AsNumber());
				MaterialInstance->SetScalarParameterValue(ParameterName, ScalarValue);
			}
			else if (Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& ArrayValue = Value->AsArray();
				if (ArrayValue.Num() < 3)
				{
					OutError = FString::Printf(TEXT("Parameter '%s' requires at least 3 numbers for vector values"), *Pair.Key);
					return false;
				}

				MaterialInstance->SetVectorParameterValue(
					ParameterName,
					FLinearColor(
						static_cast<float>(ArrayValue[0]->AsNumber()),
						static_cast<float>(ArrayValue[1]->AsNumber()),
						static_cast<float>(ArrayValue[2]->AsNumber()),
						ArrayValue.Num() >= 4 ? static_cast<float>(ArrayValue[3]->AsNumber()) : 1.0f));
			}
			else if (Value->Type == EJson::String)
			{
				FString TextureLoadError;
				UTexture* Texture = FMcpAssetModifier::LoadAssetByPath<UTexture>(Value->AsString(), TextureLoadError);
				if (!Texture)
				{
					OutError = TextureLoadError;
					return false;
				}
				MaterialInstance->SetTextureParameterValue(ParameterName, Texture);
			}
			else
			{
				OutError = FString::Printf(TEXT("Unsupported parameter override value type for '%s'"), *Pair.Key);
				return false;
			}
		}

		return true;
	}
}

FString UApplyMaterialTool::GetToolDescription() const
{
	return TEXT("Apply a material to an actor component or Blueprint component by slot index or slot name. "
		"Supports editor/PIE world targets, Blueprint component templates, and optional parameter overrides.");
}

TMap<FString, FMcpSchemaProperty> UApplyMaterialTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FJsonObject> ParameterOverridesRawSchema = MakeShareable(new FJsonObject);
	ParameterOverridesRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	ParameterOverridesRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> ParameterOverridesSchema = MakeShared<FMcpSchemaProperty>();
	ParameterOverridesSchema->Description = TEXT("Material parameter overrides. Numbers map to scalar params, numeric arrays to vector params, and strings to texture asset paths.");
	ParameterOverridesSchema->RawSchema = ParameterOverridesRawSchema;

	Schema.Add(TEXT("target_type"), FMcpSchemaProperty::MakeEnum(
		TEXT("Target type: actor, component, or blueprint_component"),
		{TEXT("actor"), TEXT("component"), TEXT("blueprint_component")}));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("Target world for actor/component targets"), {TEXT("editor"), TEXT("pie"), TEXT("auto")}));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label for actor/component targets")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Mesh component name for component or blueprint_component targets")));
	Schema.Add(TEXT("blueprint_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path for blueprint_component targets")));
	Schema.Add(TEXT("material_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Material or material instance asset path to apply"), true));
	Schema.Add(TEXT("slot_index"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Material slot index. Defaults to 0 when slot_name is omitted.")));
	Schema.Add(TEXT("slot_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional material slot name. If both slot_name and slot_index are provided, they must resolve to the same slot.")));
	Schema.Add(TEXT("create_dynamic_instance"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create a dynamic material instance on actor/component targets before applying parameter overrides")));
	Schema.Add(TEXT("parameter_overrides"), *ParameterOverridesSchema);
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save modified assets when applicable")));

	return Schema;
}

TArray<FString> UApplyMaterialTool::GetRequiredParams() const
{
	return {TEXT("material_path")};
}

FMcpToolResult UApplyMaterialTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString MaterialPath;
	if (!GetStringArg(Arguments, TEXT("material_path"), MaterialPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'material_path' required"));
	}

	const FString TargetType = GetStringArgOrDefault(Arguments, TEXT("target_type"), TEXT("actor"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const FString BlueprintPath = GetStringArgOrDefault(Arguments, TEXT("blueprint_path"));
	const FString SlotName = GetStringArgOrDefault(Arguments, TEXT("slot_name"));
	const bool bHasSlotIndex = Arguments->HasField(TEXT("slot_index"));
	const int32 RequestedSlotIndex = GetIntArgOrDefault(Arguments, TEXT("slot_index"), 0);
	const bool bCreateDynamicInstance = GetBoolArgOrDefault(Arguments, TEXT("create_dynamic_instance"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const TSharedPtr<FJsonObject>* ParameterOverridesObject = nullptr;
	Arguments->TryGetObjectField(TEXT("parameter_overrides"), ParameterOverridesObject);

	if (TargetType == TEXT("blueprint_component"))
	{
		if (BlueprintPath.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'blueprint_path' is required for target_type='blueprint_component'"));
		}
		if (ComponentName.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'component_name' is required for target_type='blueprint_component'"));
		}
		if (bCreateDynamicInstance)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSUPPORTED_COMBINATION"), TEXT("'create_dynamic_instance' is not supported for blueprint_component targets"));
		}
	}
	else
	{
		if (ActorName.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actor_name' is required for actor/component targets"));
		}
		if (TargetType == TEXT("component") && ComponentName.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'component_name' is required for target_type='component'"));
		}
	}

	FString LoadError;
	UMaterialInterface* MaterialInterface = FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(MaterialPath, LoadError);
	if (!MaterialInterface)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("apply-material"));
	Response->SetStringField(TEXT("target_type"), TargetType);
	Response->SetStringField(TEXT("material_path"), MaterialPath);

	if (TargetType == TEXT("blueprint_component"))
	{
		UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(BlueprintPath, LoadError);
		if (!Blueprint)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}

		UMeshComponent* MeshComponentTemplate = ApplyMaterialToolPrivate::FindBlueprintMeshComponentTemplate(Blueprint, ComponentName);
		if (!MeshComponentTemplate)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"),
				FString::Printf(TEXT("Blueprint component '%s' was not found or is not a mesh component"), *ComponentName));
		}

		int32 SlotIndex = 0;
		FString SlotError;
		if (!ApplyMaterialToolPrivate::ResolveSlotIndex(MeshComponentTemplate, SlotName, bHasSlotIndex, RequestedSlotIndex, SlotIndex, SlotError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MATERIAL_SLOT_NOT_FOUND"), SlotError);
		}

		if (!SlotName.IsEmpty())
		{
			MeshComponentTemplate->SetMaterialByName(FName(*SlotName), MaterialInterface);
		}
		else
		{
			MeshComponentTemplate->SetMaterial(SlotIndex, MaterialInterface);
		}

		if (ParameterOverridesObject && (*ParameterOverridesObject).IsValid())
		{
			FString ParameterError;
			if (!ApplyMaterialToolPrivate::ApplyEditorParameterOverrides(MaterialInterface, *ParameterOverridesObject, ParameterError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MATERIAL_APPLY_FAILED"), ParameterError);
			}
			ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(MaterialPath)));
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FMcpAssetModifier::MarkPackageDirty(Blueprint);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(BlueprintPath)));

		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
			if (ParameterOverridesObject && (*ParameterOverridesObject).IsValid())
			{
				if (!FMcpAssetModifier::SaveAsset(MaterialInterface, false, SaveError))
				{
					return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
				}
			}
		}

		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("blueprint_path"), BlueprintPath);
		Response->SetStringField(TEXT("component_name"), MeshComponentTemplate->GetName());
		Response->SetNumberField(TEXT("slot_index"), SlotIndex);
		if (!SlotName.IsEmpty())
		{
			Response->SetStringField(TEXT("slot_name"), SlotName);
		}
		Response->SetArrayField(TEXT("warnings"), WarningsArray);
		Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
		return FMcpToolResult::StructuredJson(Response);
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"),
			FString::Printf(TEXT("Actor '%s' not found"), *ActorName));
	}

	UMeshComponent* MeshComponent = ApplyMaterialToolPrivate::FindMeshComponent(Actor, ComponentName);
	if (!MeshComponent)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"),
			TEXT("No mesh component found for the requested actor/component target"));
	}

	int32 SlotIndex = 0;
	FString SlotError;
	if (!ApplyMaterialToolPrivate::ResolveSlotIndex(MeshComponent, SlotName, bHasSlotIndex, RequestedSlotIndex, SlotIndex, SlotError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MATERIAL_SLOT_NOT_FOUND"), SlotError);
	}

	if (bCreateDynamicInstance)
	{
		UMaterialInstanceDynamic* MaterialInstance = MeshComponent->CreateDynamicMaterialInstance(SlotIndex, MaterialInterface);
		if (!MaterialInstance)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MATERIAL_APPLY_FAILED"), TEXT("Failed to create dynamic material instance"));
		}

		if (ParameterOverridesObject && (*ParameterOverridesObject).IsValid())
		{
			FString ParameterError;
			if (!ApplyMaterialToolPrivate::ApplyDynamicParameterOverrides(MaterialInstance, *ParameterOverridesObject, ParameterError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MATERIAL_APPLY_FAILED"), ParameterError);
			}
		}

		if (bSave)
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("save=true was ignored because dynamic material instances are transient runtime/editor objects"))));
		}
	}
	else
	{
		if (ParameterOverridesObject && (*ParameterOverridesObject).IsValid())
		{
			FString ParameterError;
			if (!ApplyMaterialToolPrivate::ApplyEditorParameterOverrides(MaterialInterface, *ParameterOverridesObject, ParameterError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MATERIAL_APPLY_FAILED"), ParameterError);
			}
			ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(MaterialPath)));
		}

		if (!SlotName.IsEmpty())
		{
			MeshComponent->SetMaterialByName(FName(*SlotName), MaterialInterface);
		}
		else
		{
			MeshComponent->SetMaterial(SlotIndex, MaterialInterface);
		}

		if (bSave)
		{
			if (ParameterOverridesObject && (*ParameterOverridesObject).IsValid())
			{
				FString SaveError;
				if (!FMcpAssetModifier::SaveAsset(MaterialInterface, false, SaveError))
				{
					return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
				}
			}
			else
			{
				WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("save=true had no asset to save because actor/component material assignment is a world change rather than an asset change"))));
			}
		}
	}

	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
	Response->SetStringField(TEXT("component_name"), MeshComponent->GetName());
	Response->SetNumberField(TEXT("slot_index"), SlotIndex);
	if (!SlotName.IsEmpty())
	{
		Response->SetStringField(TEXT("slot_name"), SlotName);
	}
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
	return FMcpToolResult::StructuredJson(Response);
}