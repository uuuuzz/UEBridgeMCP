// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Landscape/EditLandscapeRegionTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "ScopedTransaction.h"

namespace
{
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

	TArray<TSharedPtr<FJsonValue>> ToIntPointArray(const FIntPoint& Point)
	{
		return {
			MakeShareable(new FJsonValueNumber(Point.X)),
			MakeShareable(new FJsonValueNumber(Point.Y))
		};
	}

	bool ResolveLandscapeRegion(ALandscapeProxy* LandscapeProxy, const TSharedPtr<FJsonObject>& OperationObject, FIntPoint& OutMin, FIntPoint& OutMax)
	{
		if (!LandscapeProxy)
		{
			return false;
		}

		const FIntRect Bounds = LandscapeProxy->GetBoundingRect();
		OutMin = Bounds.Min;
		OutMax = Bounds.Max;

		FIntPoint RequestedMin;
		FIntPoint RequestedMax;
		if (TryReadIntPointField(OperationObject, TEXT("region_min"), RequestedMin))
		{
			OutMin = RequestedMin;
		}
		if (TryReadIntPointField(OperationObject, TEXT("region_max"), RequestedMax))
		{
			OutMax = RequestedMax;
		}

		OutMin.X = FMath::Clamp(OutMin.X, Bounds.Min.X, Bounds.Max.X);
		OutMin.Y = FMath::Clamp(OutMin.Y, Bounds.Min.Y, Bounds.Max.Y);
		OutMax.X = FMath::Clamp(OutMax.X, Bounds.Min.X, Bounds.Max.X);
		OutMax.Y = FMath::Clamp(OutMax.Y, Bounds.Min.Y, Bounds.Max.Y);
		return OutMin.X <= OutMax.X && OutMin.Y <= OutMax.Y;
	}

	FString NormalizeHeightAction(const FString& Action)
	{
		return Action.ToLower();
	}
}

FString UEditLandscapeRegionTool::GetToolDescription() const
{
	return TEXT("Batch edit landscape regions with simple heightmap and layer operations, including delta, flatten, smooth, fill, and erase.");
}

TMap<FString, FMcpSchemaProperty> UEditLandscapeRegionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to edit"), { TEXT("editor"), TEXT("pie") }));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Landscape edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Landscape edit action"),
		{ TEXT("apply_height_delta"), TEXT("flatten_to_height"), TEXT("smooth"), TEXT("fill_layer"), TEXT("erase_layer") },
		true)));
	OperationSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target landscape actor label or name"))));
	OperationSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target landscape actor handle"))));
	OperationSchema->Properties.Add(TEXT("region_min"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Optional region min [x,y] in landscape coordinates"), TEXT("integer"))));
	OperationSchema->Properties.Add(TEXT("region_max"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Optional region max [x,y] in landscape coordinates"), TEXT("integer"))));
	OperationSchema->Properties.Add(TEXT("delta"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Height delta for apply_height_delta"))));
	OperationSchema->Properties.Add(TEXT("height"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Target height for flatten_to_height"))));
	OperationSchema->Properties.Add(TEXT("iterations"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Smoothing iterations"))));
	OperationSchema->Properties.Add(TEXT("layer_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Landscape layer name for fill_layer / erase_layer"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Landscape edit operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited world when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on the first failure")));
	return Schema;
}

FMcpToolResult UEditLandscapeRegionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeShareable(new FScopedTransaction(FText::FromString(TEXT("Edit Landscape Region"))));
	}

	UWorld* EditedWorld = nullptr;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		const FString Action = NormalizeHeightAction(GetStringArgOrDefault(*OperationObject, TEXT("action")));
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		UWorld* ResolvedWorld = nullptr;
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ErrorDetails;
		AActor* Actor = LevelActorToolUtils::ResolveActorReference(
			*OperationObject,
			RequestedWorldType,
			TEXT("actor_name"),
			TEXT("actor_handle"),
			Context,
			ResolvedWorld,
			ErrorCode,
			ErrorMessage,
			ErrorDetails,
			true);

		ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(Actor);
		if (!LandscapeProxy)
		{
			ResultObject->SetStringField(TEXT("error"), Actor ? TEXT("Target actor is not a LandscapeProxy") : ErrorMessage);
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		EditedWorld = EditedWorld ? EditedWorld : ResolvedWorld;
		ULandscapeInfo* LandscapeInfo = LandscapeProxy->GetLandscapeInfo();
		if (!LandscapeInfo)
		{
			ResultObject->SetStringField(TEXT("error"), TEXT("Failed to resolve LandscapeInfo"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		FIntPoint RegionMin;
		FIntPoint RegionMax;
		if (!ResolveLandscapeRegion(LandscapeProxy, *OperationObject, RegionMin, RegionMax))
		{
			ResultObject->SetStringField(TEXT("error"), TEXT("Failed to resolve a valid landscape region"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		ResultObject->SetArrayField(TEXT("region_min"), ToIntPointArray(RegionMin));
		ResultObject->SetArrayField(TEXT("region_max"), ToIntPointArray(RegionMax));
		ResultObject->SetStringField(TEXT("actor_name"), LandscapeProxy->GetActorNameOrLabel());
		ResultObject->SetBoolField(TEXT("changed"), false);

		const int32 Width = RegionMax.X - RegionMin.X + 1;
		const int32 Height = RegionMax.Y - RegionMin.Y + 1;
		const int32 Stride = Width;
		bool bOperationChanged = false;

		if (Action == TEXT("apply_height_delta") || Action == TEXT("flatten_to_height") || Action == TEXT("smooth"))
		{
			TArray<uint16> HeightData;
			HeightData.SetNumZeroed(Width * Height);

			FLandscapeEditDataInterface EditInterface(LandscapeInfo);
			EditInterface.GetHeightDataFast(RegionMin.X, RegionMin.Y, RegionMax.X, RegionMax.Y, HeightData.GetData(), Stride, nullptr, nullptr);

			if (Action == TEXT("apply_height_delta"))
			{
				const double Delta = GetFloatArgOrDefault(*OperationObject, TEXT("delta"), 0.0f);
				for (uint16& HeightValue : HeightData)
				{
					const float LocalHeight = LandscapeDataAccess::GetLocalHeight(HeightValue);
					HeightValue = LandscapeDataAccess::GetTexHeight(LocalHeight + static_cast<float>(Delta));
				}
				bOperationChanged = !FMath::IsNearlyZero(static_cast<float>(Delta));
			}
			else if (Action == TEXT("flatten_to_height"))
			{
				if (!(*OperationObject)->HasField(TEXT("height")))
				{
					ResultObject->SetStringField(TEXT("error"), TEXT("'height' is required for flatten_to_height"));
				}
				else
				{
					const float TargetHeight = GetFloatArgOrDefault(*OperationObject, TEXT("height"), 0.0f);
					const uint16 PackedHeight = LandscapeDataAccess::GetTexHeight(TargetHeight);
					for (uint16& HeightValue : HeightData)
					{
						HeightValue = PackedHeight;
					}
					bOperationChanged = true;
				}
			}
			else
			{
				const int32 Iterations = FMath::Max(1, GetIntArgOrDefault(*OperationObject, TEXT("iterations"), 1));
				for (int32 IterationIndex = 0; IterationIndex < Iterations; ++IterationIndex)
				{
					TArray<uint16> Original = HeightData;
					for (int32 Y = 0; Y < Height; ++Y)
					{
						for (int32 X = 0; X < Width; ++X)
						{
							double Total = 0.0;
							int32 SampleCount = 0;
							for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
							{
								for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
								{
									const int32 SampleX = FMath::Clamp(X + OffsetX, 0, Width - 1);
									const int32 SampleY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
									Total += LandscapeDataAccess::GetLocalHeight(Original[SampleY * Stride + SampleX]);
									++SampleCount;
								}
							}

							const float AverageHeight = static_cast<float>(Total / FMath::Max(1, SampleCount));
							HeightData[Y * Stride + X] = LandscapeDataAccess::GetTexHeight(AverageHeight);
						}
					}
				}
				bOperationChanged = true;
			}

			if (!ResultObject->HasField(TEXT("error")) && !bDryRun && bOperationChanged)
			{
				LandscapeProxy->Modify();
				EditInterface.SetHeightData(RegionMin.X, RegionMin.Y, RegionMax.X, RegionMax.Y, HeightData.GetData(), Stride, true, nullptr, nullptr, nullptr, false, nullptr, nullptr, true, true, true);
			}
		}
		else if (Action == TEXT("fill_layer") || Action == TEXT("erase_layer"))
		{
			const FString LayerName = GetStringArgOrDefault(*OperationObject, TEXT("layer_name"));
			ULandscapeLayerInfoObject* LayerInfoObject = LayerName.IsEmpty() ? nullptr : LandscapeInfo->GetLayerInfoByName(FName(*LayerName), LandscapeProxy);
			if (!LayerInfoObject)
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("A valid 'layer_name' is required for layer editing"));
			}
			else
			{
				TArray<uint8> LayerData;
				LayerData.Init(Action == TEXT("fill_layer") ? 255 : 0, Width * Height);
				if (!bDryRun)
				{
					LandscapeProxy->Modify();
					FLandscapeEditDataInterface EditInterface(LandscapeInfo);
					EditInterface.SetAlphaData(LayerInfoObject, RegionMin.X, RegionMin.Y, RegionMax.X, RegionMax.Y, LayerData.GetData(), Stride);
				}
				bOperationChanged = true;
				ResultObject->SetStringField(TEXT("layer_name"), LayerName);
			}
		}
		else
		{
			ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported action: '%s'"), *Action));
		}

		if (!ResultObject->HasField(TEXT("error")))
		{
			ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
			bAnyChanged |= bOperationChanged;
		}
		else
		{
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		if (bAnyFailed && bRollbackOnError)
		{
			break;
		}
	}

	if (!bDryRun && bAnyChanged && EditedWorld)
	{
		LevelActorToolUtils::AppendWorldModifiedAsset(EditedWorld, ModifiedAssetsArray);
	}

	FString SaveErrorCode;
	FString SaveErrorMessage;
	if (!bDryRun && bAnyChanged && EditedWorld && !LevelActorToolUtils::SaveWorldIfNeeded(EditedWorld, bSave, WarningsArray, ModifiedAssetsArray, SaveErrorCode, SaveErrorMessage))
	{
		return FMcpToolResult::StructuredError(SaveErrorCode, SaveErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
