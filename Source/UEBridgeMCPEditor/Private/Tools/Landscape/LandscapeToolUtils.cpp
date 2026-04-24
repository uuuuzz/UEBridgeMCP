// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Landscape/LandscapeToolUtils.h"

#include "Utils/McpV2ToolUtils.h"

#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"

namespace LandscapeToolUtils
{
	bool TryReadVectorField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() < 3)
		{
			return false;
		}

		OutVector.X = static_cast<float>((*Values)[0]->AsNumber());
		OutVector.Y = static_cast<float>((*Values)[1]->AsNumber());
		OutVector.Z = static_cast<float>((*Values)[2]->AsNumber());
		return true;
	}

	bool TryReadIntPointField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FIntPoint& OutPoint)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() < 2)
		{
			return false;
		}

		OutPoint.X = static_cast<int32>((*Values)[0]->AsNumber());
		OutPoint.Y = static_cast<int32>((*Values)[1]->AsNumber());
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Value)
	{
		return {
			MakeShareable(new FJsonValueNumber(Value.X)),
			MakeShareable(new FJsonValueNumber(Value.Y)),
			MakeShareable(new FJsonValueNumber(Value.Z))
		};
	}

	TArray<TSharedPtr<FJsonValue>> IntPointToArray(const FIntPoint& Value)
	{
		return {
			MakeShareable(new FJsonValueNumber(Value.X)),
			MakeShareable(new FJsonValueNumber(Value.Y))
		};
	}

	TArray<FIntPoint> ReadSamplePoints(const TSharedPtr<FJsonObject>& Object)
	{
		TArray<FIntPoint> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("sample_points"), Values) || !Values)
		{
			return Result;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* PointArray = nullptr;
			if (Value.IsValid() && Value->TryGetArray(PointArray) && PointArray && PointArray->Num() >= 2)
			{
				Result.Add(FIntPoint(
					static_cast<int32>((*PointArray)[0]->AsNumber()),
					static_cast<int32>((*PointArray)[1]->AsNumber())));
			}
		}
		return Result;
	}

	namespace
	{
		TSharedPtr<FJsonObject> BuildComponentSummary(ULandscapeComponent* Component)
		{
			TSharedPtr<FJsonObject> ComponentObject = MakeShareable(new FJsonObject);
			if (!Component)
			{
				return ComponentObject;
			}

			ComponentObject->SetStringField(TEXT("name"), Component->GetName());
			ComponentObject->SetArrayField(TEXT("section_base"), IntPointToArray(Component->GetSectionBase()));
			ComponentObject->SetArrayField(TEXT("component_key"), IntPointToArray(Component->GetComponentKey()));
			ComponentObject->SetNumberField(TEXT("component_size_quads"), Component->ComponentSizeQuads);

			if (UMaterialInterface* Material = Component->GetLandscapeMaterial())
			{
				ComponentObject->SetStringField(TEXT("material_path"), Material->GetPathName());
			}

			TArray<TSharedPtr<FJsonValue>> LayersArray;
			for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations(false))
			{
				TSharedPtr<FJsonObject> LayerObject = MakeShareable(new FJsonObject);
				if (Allocation.LayerInfo)
				{
					LayerObject->SetStringField(TEXT("name"), Allocation.LayerInfo->GetLayerName().ToString());
					LayerObject->SetStringField(TEXT("layer_info_path"), Allocation.LayerInfo->GetPathName());
				}
				else
				{
					LayerObject->SetStringField(TEXT("name"), TEXT(""));
				}
				LayerObject->SetNumberField(TEXT("weightmap_texture_index"), Allocation.WeightmapTextureIndex);
				LayerObject->SetNumberField(TEXT("weightmap_channel"), Allocation.WeightmapTextureChannel);
				LayersArray.Add(MakeShareable(new FJsonValueObject(LayerObject)));
			}
			ComponentObject->SetArrayField(TEXT("weightmap_layers"), LayersArray);
			return ComponentObject;
		}

		TArray<TSharedPtr<FJsonValue>> BuildLayerSettingsArray(ULandscapeInfo* LandscapeInfo)
		{
			TArray<TSharedPtr<FJsonValue>> LayersArray;
			if (!LandscapeInfo)
			{
				return LayersArray;
			}

			for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
			{
				TSharedPtr<FJsonObject> LayerObject = MakeShareable(new FJsonObject);
				LayerObject->SetStringField(TEXT("name"), LayerSettings.GetLayerName().ToString());
				if (LayerSettings.LayerInfoObj)
				{
					LayerObject->SetStringField(TEXT("layer_info_path"), LayerSettings.LayerInfoObj->GetPathName());
				}
				LayersArray.Add(MakeShareable(new FJsonValueObject(LayerObject)));
			}
			return LayersArray;
		}

		TArray<TSharedPtr<FJsonValue>> BuildHeightSamples(ALandscapeProxy* LandscapeProxy, const TArray<FIntPoint>& SamplePoints)
		{
			TArray<TSharedPtr<FJsonValue>> SamplesArray;
			if (!LandscapeProxy || SamplePoints.Num() == 0)
			{
				return SamplesArray;
			}

			ULandscapeInfo* LandscapeInfo = LandscapeProxy->GetLandscapeInfo();
			if (!LandscapeInfo)
			{
				return SamplesArray;
			}

			const FIntRect Bounds = LandscapeProxy->GetBoundingRect();
			FLandscapeEditDataInterface EditInterface(LandscapeInfo);
			for (const FIntPoint& SamplePoint : SamplePoints)
			{
				const FIntPoint ClampedPoint(
					FMath::Clamp(SamplePoint.X, Bounds.Min.X, Bounds.Max.X),
					FMath::Clamp(SamplePoint.Y, Bounds.Min.Y, Bounds.Max.Y));

				uint16 PackedHeight = 0;
				EditInterface.GetHeightDataFast(ClampedPoint.X, ClampedPoint.Y, ClampedPoint.X, ClampedPoint.Y, &PackedHeight, 1, nullptr, nullptr);

				TSharedPtr<FJsonObject> SampleObject = MakeShareable(new FJsonObject);
				SampleObject->SetArrayField(TEXT("requested_point"), IntPointToArray(SamplePoint));
				SampleObject->SetArrayField(TEXT("point"), IntPointToArray(ClampedPoint));
				SampleObject->SetNumberField(TEXT("packed_height"), PackedHeight);
				SampleObject->SetNumberField(TEXT("local_height"), LandscapeDataAccess::GetLocalHeight(PackedHeight));
				SamplesArray.Add(MakeShareable(new FJsonValueObject(SampleObject)));
			}
			return SamplesArray;
		}
	}

	TSharedPtr<FJsonObject> BuildLandscapeSummary(
		ALandscapeProxy* LandscapeProxy,
		const FString& SessionId,
		bool bIncludeComponents,
		int32 MaxComponents,
		const TArray<FIntPoint>& SamplePoints)
	{
		TSharedPtr<FJsonObject> LandscapeObject = MakeShareable(new FJsonObject);
		if (!LandscapeProxy)
		{
			return LandscapeObject;
		}

		LandscapeObject->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(LandscapeProxy, SessionId, true, false));
		LandscapeObject->SetStringField(TEXT("landscape_guid"), LandscapeProxy->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		LandscapeObject->SetStringField(TEXT("class"), LandscapeProxy->GetClass()->GetPathName());
		LandscapeObject->SetBoolField(TEXT("is_main_landscape"), LandscapeProxy->IsA<ALandscape>());
		LandscapeObject->SetNumberField(TEXT("component_size_quads"), LandscapeProxy->ComponentSizeQuads);
		LandscapeObject->SetNumberField(TEXT("subsection_size_quads"), LandscapeProxy->SubsectionSizeQuads);
		LandscapeObject->SetNumberField(TEXT("num_subsections"), LandscapeProxy->NumSubsections);

		const FIntRect Rect = LandscapeProxy->GetBoundingRect();
		LandscapeObject->SetArrayField(TEXT("bounding_rect_min"), IntPointToArray(Rect.Min));
		LandscapeObject->SetArrayField(TEXT("bounding_rect_max"), IntPointToArray(Rect.Max));
		LandscapeObject->SetNumberField(TEXT("resolution_x"), Rect.Width() + 1);
		LandscapeObject->SetNumberField(TEXT("resolution_y"), Rect.Height() + 1);

		TArray<ULandscapeComponent*> Components;
		LandscapeProxy->GetComponents<ULandscapeComponent>(Components);
		LandscapeObject->SetNumberField(TEXT("component_count"), Components.Num());

		if (UMaterialInterface* Material = LandscapeProxy->GetLandscapeMaterial())
		{
			LandscapeObject->SetStringField(TEXT("material_path"), Material->GetPathName());
		}

		ULandscapeInfo* LandscapeInfo = LandscapeProxy->GetLandscapeInfo();
		LandscapeObject->SetBoolField(TEXT("has_landscape_info"), LandscapeInfo != nullptr);
		if (LandscapeInfo)
		{
			LandscapeObject->SetArrayField(TEXT("layers"), BuildLayerSettingsArray(LandscapeInfo));
			const FBox CompleteBounds = LandscapeInfo->GetCompleteBounds();
			if (CompleteBounds.IsValid)
			{
				LandscapeObject->SetObjectField(TEXT("complete_bounds"), McpV2ToolUtils::SerializeBounds(
					CompleteBounds.GetCenter(),
					CompleteBounds.GetExtent(),
					static_cast<double>(CompleteBounds.GetExtent().Size())));
			}
		}

		if (TSharedPtr<FJsonObject> BoundsObject = McpV2ToolUtils::SerializeActorBounds(LandscapeProxy))
		{
			LandscapeObject->SetObjectField(TEXT("actor_bounds"), BoundsObject);
		}

		LandscapeObject->SetArrayField(TEXT("height_samples"), BuildHeightSamples(LandscapeProxy, SamplePoints));

		if (bIncludeComponents)
		{
			TArray<TSharedPtr<FJsonValue>> ComponentsArray;
			const int32 ComponentLimit = MaxComponents > 0 ? FMath::Min(MaxComponents, Components.Num()) : Components.Num();
			for (int32 ComponentIndex = 0; ComponentIndex < ComponentLimit; ++ComponentIndex)
			{
				ComponentsArray.Add(MakeShareable(new FJsonValueObject(BuildComponentSummary(Components[ComponentIndex]))));
			}
			LandscapeObject->SetArrayField(TEXT("components"), ComponentsArray);
			LandscapeObject->SetBoolField(TEXT("components_truncated"), ComponentLimit < Components.Num());
		}

		return LandscapeObject;
	}
}
