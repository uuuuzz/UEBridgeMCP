// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Protocol/McpTypes.h"

class FJsonObject;
struct FEdGraphPinType;

namespace McpTypeDescriptorUtils
{
	TSharedPtr<FMcpSchemaProperty> MakeTypeDescriptorSchema(
		const FString& Description = TEXT("Type descriptor used for variables, struct fields, and function pins"),
		int32 MaxMapNestingDepth = 1);

	bool ParseTypeDescriptor(
		const TSharedPtr<FJsonObject>& TypeObject,
		FEdGraphPinType& OutPinType,
		FString& OutError);
}
