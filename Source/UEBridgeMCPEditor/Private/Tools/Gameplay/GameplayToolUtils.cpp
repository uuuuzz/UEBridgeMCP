// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/GameplayToolUtils.h"

#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EnhancedActionKeyMapping.h"
#include "Factories/BlueprintFactory.h"
#include "IAssetTools.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"

namespace GameplayToolUtils
{
	bool ParseInputActionValueType(const FString& Value, EInputActionValueType& OutValueType)
	{
		if (Value.Equals(TEXT("bool"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Boolean;
			return true;
		}
		if (Value.Equals(TEXT("axis1d"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("1d"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Axis1D;
			return true;
		}
		if (Value.Equals(TEXT("axis2d"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("2d"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Axis2D;
			return true;
		}
		if (Value.Equals(TEXT("axis3d"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("3d"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Axis3D;
			return true;
		}
		return false;
	}

	FString InputActionValueTypeToString(EInputActionValueType ValueType)
	{
		switch (ValueType)
		{
		case EInputActionValueType::Boolean:
			return TEXT("bool");
		case EInputActionValueType::Axis1D:
			return TEXT("axis1d");
		case EInputActionValueType::Axis2D:
			return TEXT("axis2d");
		case EInputActionValueType::Axis3D:
			return TEXT("axis3d");
		default:
			return TEXT("unknown");
		}
	}

	bool ParseKey(const FString& KeyName, FKey& OutKey, FString& OutError)
	{
		if (KeyName.IsEmpty())
		{
			OutError = TEXT("Key name cannot be empty");
			return false;
		}

		const FKey ParsedKey(*KeyName);
		if (!ParsedKey.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid input key '%s'"), *KeyName);
			return false;
		}

		OutKey = ParsedKey;
		return true;
	}

	TSharedPtr<FJsonObject> SerializeInputMapping(const FEnhancedActionKeyMapping& Mapping)
	{
		TSharedPtr<FJsonObject> MappingObject = MakeShareable(new FJsonObject);
		MappingObject->SetStringField(TEXT("key"), Mapping.Key.ToString());
		if (Mapping.Action)
		{
			MappingObject->SetStringField(TEXT("action_name"), Mapping.Action->GetName());
			MappingObject->SetStringField(TEXT("action_asset_path"), Mapping.Action->GetPathName());
		}
		MappingObject->SetNumberField(TEXT("modifier_count"), Mapping.Modifiers.Num());
		MappingObject->SetNumberField(TEXT("trigger_count"), Mapping.Triggers.Num());
		return MappingObject;
	}

	UObject* CreateObjectAsset(UClass* AssetClass, const FString& AssetPath, FString& OutError)
	{
		if (!AssetClass)
		{
			OutError = TEXT("Asset class is required");
			return nullptr;
		}

		if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, OutError))
		{
			return nullptr;
		}

		if (FMcpAssetModifier::AssetExists(AssetPath))
		{
			OutError = FString::Printf(TEXT("Asset already exists: %s"), *AssetPath);
			return nullptr;
		}

		const FString AssetName = FPackageName::GetShortName(AssetPath);
		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package)
		{
			OutError = TEXT("Failed to create package");
			return nullptr;
		}

		UObject* CreatedAsset = NewObject<UObject>(Package, AssetClass, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!CreatedAsset)
		{
			OutError = FString::Printf(TEXT("Failed to create asset of class %s"), *AssetClass->GetName());
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(CreatedAsset);
		FMcpAssetModifier::MarkPackageDirty(CreatedAsset);
		return CreatedAsset;
	}

	UBlueprint* CreateBlueprintAsset(const FString& AssetPath, UClass* ParentClass, FString& OutError)
	{
		if (!ParentClass)
		{
			OutError = TEXT("Parent class is required");
			return nullptr;
		}

		if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, OutError))
		{
			return nullptr;
		}

		if (FMcpAssetModifier::AssetExists(AssetPath))
		{
			OutError = FString::Printf(TEXT("Asset already exists: %s"), *AssetPath);
			return nullptr;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		const FString AssetName = FPackageName::GetShortName(AssetPath);

		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = ParentClass;

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UObject* CreatedObject = AssetTools.CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
		UBlueprint* CreatedBlueprint = Cast<UBlueprint>(CreatedObject);
		if (!CreatedBlueprint)
		{
			OutError = FString::Printf(TEXT("Failed to create Blueprint asset: %s"), *AssetPath);
			return nullptr;
		}

		FMcpAssetModifier::MarkPackageDirty(CreatedBlueprint);
		return CreatedBlueprint;
	}

	TSharedPtr<FJsonObject> BuildBatchFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> PartialObject = MakeShareable(new FJsonObject);
		PartialObject->SetStringField(TEXT("tool"), ToolName);
		PartialObject->SetArrayField(TEXT("results"), ResultsArray);
		PartialObject->SetArrayField(TEXT("warnings"), WarningsArray);
		PartialObject->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		PartialObject->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		PartialObject->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return PartialObject;
	}
}
