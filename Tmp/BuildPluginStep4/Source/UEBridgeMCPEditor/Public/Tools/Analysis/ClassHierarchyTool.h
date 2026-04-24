// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "ClassHierarchyTool.generated.h"

/**
 * Tool for browsing class inheritance tree
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UClassHierarchyTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("get-class-hierarchy"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;

private:
	/** Find class by name (supports both C++ and Blueprint classes) */
	UClass* FindClassByName(const FString& ClassName) const;

	/** Get parent classes up to UObject */
	TArray<UClass*> GetParentClasses(UClass* Class, int32 MaxDepth) const;

	/** Get child classes (optionally including Blueprints) */
	TArray<UClass*> GetChildClasses(UClass* Class, bool bIncludeBlueprints, int32 MaxDepth) const;

	/** Convert class to JSON */
	TSharedPtr<FJsonObject> ClassToJson(UClass* Class) const;

	/** Build class hierarchy JSON */
	TSharedPtr<FJsonObject> BuildHierarchyJson(UClass* Class, const FString& Direction, bool bIncludeBlueprints, int32 Depth) const;
};
