// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class ALandscapeProxy;

namespace LandscapeToolUtils
{
	bool TryReadVectorField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector);
	bool TryReadIntPointField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FIntPoint& OutPoint);
	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Value);
	TArray<TSharedPtr<FJsonValue>> IntPointToArray(const FIntPoint& Value);
	TArray<FIntPoint> ReadSamplePoints(const TSharedPtr<FJsonObject>& Object);
	TSharedPtr<FJsonObject> BuildLandscapeSummary(
		ALandscapeProxy* LandscapeProxy,
		const FString& SessionId,
		bool bIncludeComponents,
		int32 MaxComponents,
		const TArray<FIntPoint>& SamplePoints);
}
