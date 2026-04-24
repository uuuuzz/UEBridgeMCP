// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/StaticMesh/StaticMeshAdvancedTools.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/EngineVersionComparison.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FJsonObject> SerializeStaticMaterialSlot(const FStaticMaterial& Slot, int32 Index)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("slot_name"), Slot.MaterialSlotName.ToString());
		Object->SetStringField(TEXT("imported_slot_name"), Slot.ImportedMaterialSlotName.ToString());
		Object->SetStringField(TEXT("material_path"), Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : TEXT(""));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SerializeStaticMaterialSlots(const UStaticMesh* StaticMesh)
	{
		TArray<TSharedPtr<FJsonValue>> SlotsArray;
		if (!StaticMesh)
		{
			return SlotsArray;
		}

		const TArray<FStaticMaterial>& Slots = StaticMesh->GetStaticMaterials();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			SlotsArray.Add(MakeShareable(new FJsonValueObject(SerializeStaticMaterialSlot(Slots[Index], Index))));
		}
		return SlotsArray;
	}

	bool IsNaniteEnabled(const UStaticMesh* StaticMesh)
	{
		if (!StaticMesh)
		{
			return false;
		}
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		return StaticMesh->NaniteSettings.bEnabled;
#else
		return StaticMesh->GetNaniteSettings().bEnabled;
#endif
	}

	int32 ResolveMaterialSlotIndex(const UStaticMesh* StaticMesh, const TSharedPtr<FJsonObject>& Operation)
	{
		if (!StaticMesh || !Operation.IsValid())
		{
			return INDEX_NONE;
		}

		int32 SlotIndex = INDEX_NONE;
		if (Operation->TryGetNumberField(TEXT("slot_index"), SlotIndex))
		{
			return SlotIndex;
		}

		FString SlotName;
		if (!Operation->TryGetStringField(TEXT("slot_name"), SlotName) || SlotName.IsEmpty())
		{
			return INDEX_NONE;
		}

		const TArray<FStaticMaterial>& Slots = StaticMesh->GetStaticMaterials();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Slots[Index].MaterialSlotName.ToString().Equals(SlotName, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

FString UQueryMeshComplexityTool::GetToolDescription() const
{
	return TEXT("Return static mesh complexity signals across LODs, material slots, Nanite, and bounds.");
}

TMap<FString, FMcpSchemaProperty> UQueryMeshComplexityTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Static mesh asset path"), true));
	return Schema;
}

FMcpToolResult UQueryMeshComplexityTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	FString LoadError;
	UStaticMesh* StaticMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(AssetPath, LoadError);
	if (!StaticMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	const int32 LodCount = StaticMesh->GetNumLODs();
	int64 TotalTrianglesAcrossLods = 0;
	int32 MaxLodTriangles = 0;
	TArray<TSharedPtr<FJsonValue>> LodsArray;
	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		const int32 TriangleCount = StaticMesh->GetNumTriangles(LodIndex);
		TotalTrianglesAcrossLods += TriangleCount;
		MaxLodTriangles = FMath::Max(MaxLodTriangles, TriangleCount);

		TSharedPtr<FJsonObject> LodObject = MakeShareable(new FJsonObject);
		LodObject->SetNumberField(TEXT("lod_index"), LodIndex);
		LodObject->SetNumberField(TEXT("triangle_count"), TriangleCount);
		LodsArray.Add(MakeShareable(new FJsonValueObject(LodObject)));
	}

	const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
	const int32 SlotCount = StaticMesh->GetStaticMaterials().Num();
	const double BoundsVolume = (Bounds.BoxExtent * 2.0).X * (Bounds.BoxExtent * 2.0).Y * (Bounds.BoxExtent * 2.0).Z;

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	if (MaxLodTriangles > 100000)
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("LOD0 triangle count is high for real-time use; consider Nanite or additional LOD reduction."))));
	}
	if (SlotCount > 8)
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("Material slot count is high; each slot can increase draw-call pressure."))));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, StaticMesh->GetClass()->GetName()));
	Response->SetNumberField(TEXT("lod_count"), LodCount);
	Response->SetNumberField(TEXT("lod0_triangles"), LodCount > 0 ? StaticMesh->GetNumTriangles(0) : 0);
	Response->SetNumberField(TEXT("total_triangles_across_lods"), static_cast<double>(TotalTrianglesAcrossLods));
	Response->SetNumberField(TEXT("material_slot_count"), SlotCount);
	Response->SetBoolField(TEXT("nanite_enabled"), IsNaniteEnabled(StaticMesh));
	Response->SetObjectField(TEXT("bounds"), McpV2ToolUtils::SerializeBounds(Bounds.Origin, Bounds.BoxExtent, Bounds.SphereRadius));
	Response->SetNumberField(TEXT("bounds_volume"), BoundsVolume);
	Response->SetArrayField(TEXT("lods"), LodsArray);
	Response->SetArrayField(TEXT("material_slots"), SerializeStaticMaterialSlots(StaticMesh));
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	return FMcpToolResult::StructuredJson(Response);
}

FString UEditStaticMeshSlotsTool::GetToolDescription() const
{
	return TEXT("Batch edit static mesh material slots: assign materials, rename slots, add slots, or remove trailing unused slots.");
}

TMap<FString, FMcpSchemaProperty> UEditStaticMeshSlotsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Static mesh asset path"), true));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("Material slot edit operations"), TEXT("object")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the mesh after edits. Default: true.")));
	return Schema;
}

FMcpToolResult UEditStaticMeshSlotsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UStaticMesh* StaticMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(AssetPath, LoadError);
	if (!StaticMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Static Mesh Material Slots")));
		StaticMesh->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bChanged = false;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		Action = Action.ToLower();

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bOperationSuccess = false;
		FString OperationError;

		if (Action == TEXT("set_material"))
		{
			const int32 SlotIndex = ResolveMaterialSlotIndex(StaticMesh, *OperationObject);
			const FString MaterialPath = GetStringArgOrDefault(*OperationObject, TEXT("material_path"));
			FString MaterialError;
			UMaterialInterface* Material = FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(MaterialPath, MaterialError);
			if (!StaticMesh->GetStaticMaterials().IsValidIndex(SlotIndex))
			{
				OperationError = TEXT("Valid slot_index or slot_name is required");
			}
			else if (!Material)
			{
				OperationError = MaterialError;
			}
			else
			{
				if (!bDryRun)
				{
					StaticMesh->SetMaterial(SlotIndex, Material);
				}
				ResultObject->SetNumberField(TEXT("slot_index"), SlotIndex);
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("rename_slot"))
		{
			const int32 SlotIndex = ResolveMaterialSlotIndex(StaticMesh, *OperationObject);
			const FString NewSlotName = GetStringArgOrDefault(*OperationObject, TEXT("new_slot_name"));
			if (!StaticMesh->GetStaticMaterials().IsValidIndex(SlotIndex))
			{
				OperationError = TEXT("Valid slot_index or slot_name is required");
			}
			else if (NewSlotName.IsEmpty())
			{
				OperationError = TEXT("'new_slot_name' is required");
			}
			else
			{
				if (!bDryRun)
				{
					TArray<FStaticMaterial>& Slots = StaticMesh->GetStaticMaterials();
					Slots[SlotIndex].MaterialSlotName = FName(*NewSlotName);
					if (Slots[SlotIndex].ImportedMaterialSlotName.IsNone())
					{
						Slots[SlotIndex].ImportedMaterialSlotName = FName(*NewSlotName);
					}
				}
				ResultObject->SetNumberField(TEXT("slot_index"), SlotIndex);
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("add_slot"))
		{
			const FString SlotName = GetStringArgOrDefault(*OperationObject, TEXT("slot_name"), TEXT("MaterialSlot"));
			const FString MaterialPath = GetStringArgOrDefault(*OperationObject, TEXT("material_path"));
			UMaterialInterface* Material = nullptr;
			if (!MaterialPath.IsEmpty())
			{
				FString MaterialError;
				Material = FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(MaterialPath, MaterialError);
				if (!Material)
				{
					OperationError = MaterialError;
				}
			}

			if (OperationError.IsEmpty())
			{
				if (!bDryRun)
				{
					TArray<FStaticMaterial>& Slots = StaticMesh->GetStaticMaterials();
					Slots.Add(FStaticMaterial(Material, FName(*SlotName), FName(*SlotName)));
					ResultObject->SetNumberField(TEXT("slot_index"), Slots.Num() - 1);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("remove_trailing_slot"))
		{
			const int32 SlotIndex = ResolveMaterialSlotIndex(StaticMesh, *OperationObject);
			if (!StaticMesh->GetStaticMaterials().IsValidIndex(SlotIndex))
			{
				OperationError = TEXT("Valid slot_index or slot_name is required");
			}
			else if (SlotIndex != StaticMesh->GetStaticMaterials().Num() - 1)
			{
				OperationError = TEXT("Only the trailing material slot can be removed safely by this tool");
			}
			else
			{
				if (!bDryRun)
				{
					StaticMesh->GetStaticMaterials().RemoveAt(SlotIndex);
				}
				ResultObject->SetNumberField(TEXT("slot_index"), SlotIndex);
				bOperationSuccess = true;
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		if (bOperationSuccess)
		{
			bChanged = true;
		}
		else
		{
			bAnyFailed = true;
			ResultObject->SetStringField(TEXT("error"), OperationError);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		StaticMesh->PostEditChange();
		FMcpAssetModifier::MarkPackageDirty(StaticMesh);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(StaticMesh, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("material_slots"), SerializeStaticMaterialSlots(StaticMesh));
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
