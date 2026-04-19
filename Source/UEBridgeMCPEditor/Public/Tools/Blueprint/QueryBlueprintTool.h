// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryBlueprintTool.generated.h"

/**
 * Consolidated tool for Blueprint structure analysis.
 * Replaces: analyze-blueprint, get-blueprint-functions, get-blueprint-variables,
 *           get-blueprint-components, get-blueprint-defaults
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryBlueprintTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-blueprint"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	// === Structure extraction ===

	/** Extract function information */
	TSharedPtr<FJsonObject> ExtractFunctions(class UBlueprint* Blueprint, bool bDetailed) const;

	/** Extract variable information */
	TSharedPtr<FJsonObject> ExtractVariables(class UBlueprint* Blueprint, bool bDetailed) const;

	/** Extract component information */
	TSharedPtr<FJsonObject> ExtractComponents(class UBlueprint* Blueprint, bool bDetailed) const;

	/** Extract event graph summary */
	TSharedPtr<FJsonObject> ExtractEventGraphSummary(class UBlueprint* Blueprint) const;

	/** Extract CDO defaults */
	TSharedPtr<FJsonObject> ExtractDefaults(class UBlueprint* Blueprint, bool bIncludeInherited, const FString& CategoryFilter, const FString& PropertyFilter) const;

	/** Extract component instance overrides */
	TSharedPtr<FJsonObject> ExtractComponentOverrides(class UBlueprint* Blueprint, const FString& ComponentFilter, const FString& PropertyFilter, bool bIncludeNonOverridden) const;

	// === Helpers ===

	/** Convert property to JSON */
	TSharedPtr<FJsonObject> PropertyToJson(class FProperty* Property, void* Container, UObject* Owner) const;

	/** Get property type as string */
	FString GetPropertyTypeString(class FProperty* Property) const;
};
