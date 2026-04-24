// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CreateAssetTool.generated.h"

class IAssetTools;

/**
 * Create a new asset by class name.
 * Supports any UObject type including Blueprint, Material, DataTable, DataAsset, etc.
 * Uses dynamic class resolution - accepts class names or Blueprint class paths.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UCreateAssetTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("create-asset"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("write"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool RequiresGameThread() const override { return true; }

private:
	// Create asset when class is resolved
		UObject* CreateAssetOfClass(
			UClass* AssetClass,
			const FString& AssetPath,
			const FString& PackagePath,
			const FString& AssetName,
			const FString& ParentClass,
			const FString& RowStruct,
			const FString& StaticMeshPath,
			IAssetTools& AssetTools,
			TSharedPtr<FJsonObject>& Result,
			FString& OutError);

	// Create asset by string name (fallback for unresolved names)
	UObject* CreateAssetByName(
		const FString& AssetClassName,
		const FString& AssetPath,
		const FString& PackagePath,
			const FString& AssetName,
			const FString& ParentClass,
			const FString& RowStruct,
			const FString& StaticMeshPath,
			IAssetTools& AssetTools,
			TSharedPtr<FJsonObject>& Result,
			FString& OutError);

	// Specialized creators
	UObject* CreateBlueprint(const FString& PackagePath, const FString& AssetName, const FString& ParentClassName, IAssetTools& AssetTools, TSharedPtr<FJsonObject>& Result, FString& OutError);
	UObject* CreateBlueprintInterface(const FString& PackagePath, const FString& AssetName, IAssetTools& AssetTools, FString& OutError);
	UObject* CreateMaterial(const FString& PackagePath, const FString& AssetName, IAssetTools& AssetTools, FString& OutError);
	UObject* CreateDataTable(const FString& PackagePath, const FString& AssetName, const FString& RowStruct, IAssetTools& AssetTools, FString& OutError);
	UObject* CreateLevel(const FString& PackagePath, const FString& AssetName, IAssetTools& AssetTools, FString& OutError);
	UObject* CreateLevelSequence(const FString& AssetPath, const FString& AssetName, FString& OutError);
	UObject* CreateFoliageTypeInstancedStaticMesh(const FString& AssetPath, const FString& AssetName, const FString& StaticMeshPath, TSharedPtr<FJsonObject>& Result, FString& OutError);
	UObject* CreateWidgetBlueprint(const FString& PackagePath, const FString& AssetName, IAssetTools& AssetTools, TSharedPtr<FJsonObject>& Result, FString& OutError);
	UObject* CreateAnimBlueprint(const FString& PackagePath, const FString& AssetName, const FString& SkeletonPath, IAssetTools& AssetTools, TSharedPtr<FJsonObject>& Result, FString& OutError);
	UObject* CreateDataAsset(const FString& AssetPath, const FString& AssetName, UClass* DataAssetClass, FString& OutError);
	UObject* CreateGenericAsset(const FString& AssetPath, const FString& AssetName, UClass* AssetClass, IAssetTools& AssetTools, FString& OutError);
};
