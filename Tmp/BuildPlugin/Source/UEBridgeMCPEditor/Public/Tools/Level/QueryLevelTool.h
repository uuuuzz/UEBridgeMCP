// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryLevelTool.generated.h"

/**
 * Tool for querying actors in the currently open level.
 * Can list actors with filtering, or get detailed info for a specific actor.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryLevelTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-level"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	// === List mode (multiple actors) ===

	/** Convert actor to basic JSON for list mode */
	TSharedPtr<FJsonObject> ActorToJson(class AActor* Actor, bool bIncludeComponents, bool bIncludeTransform) const;

	/** Check if actor matches class filter (wildcard support) */
	bool MatchesClassFilter(class AActor* Actor, const FString& Filter) const;

	/** Check if actor matches folder filter */
	bool MatchesFolderFilter(class AActor* Actor, const FString& Filter) const;

	/** Check if actor matches tag filter */
	bool MatchesTagFilter(class AActor* Actor, const FString& Filter) const;

	/** Check if string matches wildcard pattern */
	bool MatchesWildcard(const FString& Name, const FString& Pattern) const;

	// === Detail mode (single actor) ===

	/** Convert actor to detailed JSON with properties */
	TSharedPtr<FJsonObject> ActorToDetailedJson(class AActor* Actor, bool bIncludeProperties, bool bIncludeComponents, bool bIncludeInherited) const;

	/** Convert component to detailed JSON */
	TSharedPtr<FJsonObject> ComponentToDetailedJson(class UActorComponent* Component, bool bIncludeProperties, bool bIncludeInherited) const;

	/** Convert property to JSON */
	TSharedPtr<FJsonObject> PropertyToJson(class FProperty* Property, void* ValuePtr, UObject* Owner) const;

	/** Get property type as string */
	FString GetPropertyTypeString(class FProperty* Property) const;

	// === External level loading ===

	/** Query actors in an external level asset without opening it in the editor */
	FMcpToolResult QueryExternalLevel(const FString& LevelPath, const TSharedPtr<FJsonObject>& Arguments) const;

	// === Shared ===

	/** Get transform as JSON */
	TSharedPtr<FJsonObject> TransformToJson(const FTransform& Transform) const;
};
