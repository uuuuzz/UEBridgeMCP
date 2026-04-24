// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UObject;
class UMaterial;
class UMaterialExpression;
struct FExpressionInput;
enum EMaterialProperty : int;

namespace MaterialToolUtils
{
	TSharedPtr<FJsonObject> BuildGraphOverview(UMaterial* Material, bool bIncludePositions);

	bool ResolveExpressionClass(const FString& RequestedClassName, UClass*& OutExpressionClass, FString& OutError);
	bool ApplyObjectProperties(UObject* Object, const TSharedPtr<FJsonObject>& Properties, FString& OutError);

	UMaterialExpression* FindExpressionByName(UMaterial* Material, const FString& ExpressionName);
	bool ResolveExpressionInput(UMaterialExpression* Expression, const FString& RequestedInputName, FExpressionInput*& OutInput, FString& OutError);
	bool ResolveExpressionOutputName(UMaterialExpression* Expression, const FString& RequestedOutput, FString& OutOutputName, int32& OutOutputIndex, FString& OutError);

	bool TryParseMaterialProperty(const FString& Value, EMaterialProperty& OutProperty);
	FString MaterialPropertyToString(EMaterialProperty Property);
}
