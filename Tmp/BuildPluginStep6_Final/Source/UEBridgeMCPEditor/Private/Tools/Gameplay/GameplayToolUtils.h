#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "InputActionValue.h"

class UBlueprint;
class UInputAction;
class UInputMappingContext;
struct FEnhancedActionKeyMapping;

namespace GameplayToolUtils
{
	bool ParseInputActionValueType(const FString& Value, EInputActionValueType& OutValueType);
	FString InputActionValueTypeToString(EInputActionValueType ValueType);

	bool ParseKey(const FString& KeyName, FKey& OutKey, FString& OutError);

	TSharedPtr<FJsonObject> SerializeInputMapping(const FEnhancedActionKeyMapping& Mapping);

	UObject* CreateObjectAsset(UClass* AssetClass, const FString& AssetPath, FString& OutError);
	UBlueprint* CreateBlueprintAsset(const FString& AssetPath, UClass* ParentClass, FString& OutError);

	TSharedPtr<FJsonObject> BuildBatchFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray);
}
