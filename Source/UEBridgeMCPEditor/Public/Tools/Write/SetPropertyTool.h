// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "SetPropertyTool.generated.h"

/**
 * Universal property setter tool.
 * Sets any property on any asset type using UE reflection.
 *
 * Supports:
 * - Simple properties (int, float, bool, string, name, text)
 * - Nested properties via dot notation (e.g., "Stats.MaxHealth")
 * - Array elements via bracket notation (e.g., "Items[0].Value")
 * - Struct properties (as JSON objects or arrays for FVector, FRotator, FLinearColor)
 * - Enum properties (by name or integer value)
 *
 * Examples:
 * - Blueprint variable: { "asset_path": "/Game/BP_Player", "property_path": "Health", "value": 100 }
 * - Nested struct: { "asset_path": "/Game/BP_Player", "property_path": "Stats.MaxHealth", "value": 200 }
 * - Vector: { "asset_path": "/Game/BP_Actor", "property_path": "Location", "value": [100, 200, 0] }
 * - Enum: { "asset_path": "/Game/BP_Actor", "property_path": "State", "value": "Active" }
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API USetPropertyTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("set-property"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }
};
