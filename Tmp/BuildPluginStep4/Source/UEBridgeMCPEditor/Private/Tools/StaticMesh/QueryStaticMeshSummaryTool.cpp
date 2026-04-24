// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/StaticMesh/QueryStaticMeshSummaryTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshEditorSubsystem.h"

FString UQueryStaticMeshSummaryTool::GetToolDescription() const
{
	return TEXT("Return compact static mesh summaries including bounds, material slots, LOD settings, Nanite state, and per-LOD triangle counts.");
}

TMap<FString, FMcpSchemaProperty> UQueryStaticMeshSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Static mesh asset path"), true));
	return Schema;
}

TArray<FString> UQueryStaticMeshSummaryTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UQueryStaticMeshSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	FString LoadError;
	UStaticMesh* StaticMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(AssetPath, LoadError);
	if (!StaticMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UStaticMeshEditorSubsystem* StaticMeshSubsystem = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;

	const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
	const int32 LodCount = StaticMeshSubsystem ? StaticMeshSubsystem->GetLodCount(StaticMesh) : StaticMesh->GetNumLODs();
	const TArray<float> LodScreenSizes = StaticMeshSubsystem ? StaticMeshSubsystem->GetLodScreenSizes(StaticMesh) : TArray<float>();

	TArray<TSharedPtr<FJsonValue>> MaterialSlotsArray;
	const TArray<FStaticMaterial>& MaterialSlots = StaticMesh->GetStaticMaterials();
	for (int32 SlotIndex = 0; SlotIndex < MaterialSlots.Num(); ++SlotIndex)
	{
		const FStaticMaterial& Slot = MaterialSlots[SlotIndex];
		TSharedPtr<FJsonObject> SlotObject = MakeShareable(new FJsonObject);
		SlotObject->SetNumberField(TEXT("index"), SlotIndex);
		SlotObject->SetStringField(TEXT("slot_name"), Slot.MaterialSlotName.ToString());
		SlotObject->SetStringField(TEXT("imported_slot_name"), Slot.ImportedMaterialSlotName.ToString());
		SlotObject->SetStringField(TEXT("material_path"), Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : TEXT(""));
		MaterialSlotsArray.Add(MakeShareable(new FJsonValueObject(SlotObject)));
	}

	TArray<TSharedPtr<FJsonValue>> LodScreenSizeArray;
	for (const float ScreenSize : LodScreenSizes)
	{
		LodScreenSizeArray.Add(MakeShareable(new FJsonValueNumber(ScreenSize)));
	}

	TArray<TSharedPtr<FJsonValue>> TriangleCountsArray;
	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		TriangleCountsArray.Add(MakeShareable(new FJsonValueNumber(StaticMesh->GetNumTriangles(LodIndex))));
	}

	TSharedPtr<FJsonObject> NaniteObject = MakeShareable(new FJsonObject);
	if (StaticMeshSubsystem)
	{
		const FMeshNaniteSettings NaniteSettings = StaticMeshSubsystem->GetNaniteSettings(StaticMesh);
		NaniteObject->SetBoolField(TEXT("enabled"), NaniteSettings.bEnabled);
	}
	else
	{
		NaniteObject->SetBoolField(TEXT("enabled"), StaticMesh->NaniteSettings.bEnabled);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, StaticMesh->GetClass()->GetName()));
	Result->SetObjectField(TEXT("bounds"), McpV2ToolUtils::SerializeBounds(Bounds.Origin, Bounds.BoxExtent, Bounds.SphereRadius));
	Result->SetArrayField(TEXT("material_slots"), MaterialSlotsArray);
	Result->SetNumberField(TEXT("lod_count"), LodCount);
	Result->SetStringField(TEXT("lod_group"), StaticMeshSubsystem ? StaticMeshSubsystem->GetLODGroup(StaticMesh).ToString() : TEXT(""));
	Result->SetArrayField(TEXT("lod_screen_sizes"), LodScreenSizeArray);
	Result->SetObjectField(TEXT("nanite"), NaniteObject);
	Result->SetArrayField(TEXT("triangle_counts"), TriangleCountsArray);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Static mesh summary ready"));
}
