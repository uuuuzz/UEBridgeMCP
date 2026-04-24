// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/QueryAssetTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "Utils/McpDataTableUtils.h"
#include "Tools/McpToolResult.h"
#include "UEBridgeMCPEditor.h"

FString UQueryAssetTool::GetToolDescription() const
{
	return TEXT("Query assets: search by pattern/class/path, or inspect a specific asset. "
		"Use 'query' for search mode, 'asset_path' for inspect mode.");
}

TMap<FString, FMcpSchemaProperty> UQueryAssetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	// Search mode parameters
	FMcpSchemaProperty Query;
	Query.Type = TEXT("string");
	Query.Description = TEXT("Search query (asset name pattern, supports * and ? wildcards). Triggers search mode.");
	Query.bRequired = false;
	Schema.Add(TEXT("query"), Query);

	FMcpSchemaProperty ClassFilter;
	ClassFilter.Type = TEXT("string");
	ClassFilter.Description = TEXT("Filter by asset class (e.g., Blueprint, StaticMesh, Material)");
	ClassFilter.bRequired = false;
	Schema.Add(TEXT("class"), ClassFilter);

	FMcpSchemaProperty PathFilter;
	PathFilter.Type = TEXT("string");
	PathFilter.Description = TEXT("Filter by path prefix (e.g., /Game/Blueprints)");
	PathFilter.bRequired = false;
	Schema.Add(TEXT("path"), PathFilter);

	FMcpSchemaProperty Limit;
	Limit.Type = TEXT("integer");
	Limit.Description = TEXT("Maximum results for search (default: 100)");
	Limit.bRequired = false;
	Schema.Add(TEXT("limit"), Limit);

	// Inspect mode parameters
	FMcpSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to inspect (e.g., /Game/Data/DT_Items). Triggers inspect mode.");
	AssetPath.bRequired = false;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FMcpSchemaProperty Depth;
	Depth.Type = TEXT("integer");
	Depth.Description = TEXT("Recursion depth for nested objects (default: 2, max: 5). For inspect mode.");
	Depth.bRequired = false;
	Schema.Add(TEXT("depth"), Depth);

	FMcpSchemaProperty IncludeDefaults;
	IncludeDefaults.Type = TEXT("boolean");
	IncludeDefaults.Description = TEXT("Include properties with default/empty values (default: false)");
	IncludeDefaults.bRequired = false;
	Schema.Add(TEXT("include_defaults"), IncludeDefaults);

	FMcpSchemaProperty PropertyFilter;
	PropertyFilter.Type = TEXT("string");
	PropertyFilter.Description = TEXT("Filter properties by name (wildcards supported)");
	PropertyFilter.bRequired = false;
	Schema.Add(TEXT("property_filter"), PropertyFilter);

	FMcpSchemaProperty CategoryFilter;
	CategoryFilter.Type = TEXT("string");
	CategoryFilter.Description = TEXT("Filter by UPROPERTY category");
	CategoryFilter.bRequired = false;
	Schema.Add(TEXT("category_filter"), CategoryFilter);

	FMcpSchemaProperty RowFilter;
	RowFilter.Type = TEXT("string");
	RowFilter.Description = TEXT("Filter DataTable rows by name (wildcards supported)");
	RowFilter.bRequired = false;
	Schema.Add(TEXT("row_filter"), RowFilter);

	return Schema;
}

TArray<FString> UQueryAssetTool::GetRequiredParams() const
{
	return {}; // Either query or asset_path must be provided
}

FMcpToolResult UQueryAssetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString Query = GetStringArgOrDefault(Arguments, TEXT("query"), TEXT(""));
	FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));

	// Determine mode
	if (!AssetPath.IsEmpty())
	{
		// Inspect mode
		int32 MaxDepth = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("depth"), 2), 1, 5);
		bool bIncludeDefaults = GetBoolArgOrDefault(Arguments, TEXT("include_defaults"), false);
		FString PropertyFilter = GetStringArgOrDefault(Arguments, TEXT("property_filter"), TEXT(""));
		FString CategoryFilter = GetStringArgOrDefault(Arguments, TEXT("category_filter"), TEXT(""));
		FString RowFilter = GetStringArgOrDefault(Arguments, TEXT("row_filter"), TEXT(""));

		return InspectAsset(AssetPath, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter, RowFilter);
	}

	// Search mode
	FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class"), TEXT(""));
	FString PathFilter = GetStringArgOrDefault(Arguments, TEXT("path"), TEXT(""));
	int32 Limit = GetIntArgOrDefault(Arguments, TEXT("limit"), 100);

	if (Query.IsEmpty() && ClassFilter.IsEmpty() && PathFilter.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("Provide 'asset_path' to inspect, or 'query'/'class'/'path' to search"));
	}

	return SearchAssets(Query, ClassFilter, PathFilter, Limit);
}

// === Search mode ===

FMcpToolResult UQueryAssetTool::SearchAssets(const FString& Query, const FString& ClassFilter,
	const FString& PathFilter, int32 Limit) const
{
	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-asset search: query='%s', class='%s', path='%s'"),
		*Query, *ClassFilter, *PathFilter);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;

	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
		Filter.bRecursivePaths = true;
	}

	if (!ClassFilter.IsEmpty())
	{
		UClass* FilterClass = FindFirstObjectSafe<UClass>(*ClassFilter);
		if (!FilterClass)
		{
			FilterClass = FindFirstObjectSafe<UClass>(*(TEXT("U") + ClassFilter));
		}
		if (!FilterClass)
		{
			FilterClass = FindFirstObjectSafe<UClass>(*(TEXT("A") + ClassFilter));
		}

		if (FilterClass)
		{
			Filter.ClassPaths.Add(FilterClass->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	// Apply name filter if specified
	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	int32 Count = 0;

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (Count >= Limit) break;

		// Apply name filter
		if (!Query.IsEmpty())
		{
			FString AssetName = AssetData.AssetName.ToString();
			if (!MatchesWildcard(AssetName, Query))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> AssetJson = MakeShareable(new FJsonObject);
		AssetJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetJson->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

		AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
		Count++;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());
	Result->SetNumberField(TEXT("total_matching"), AssetDataList.Num());
	Result->SetBoolField(TEXT("limit_reached"), Count >= Limit);

	return FMcpToolResult::Json(Result);
}

// === Inspect mode ===

FMcpToolResult UQueryAssetTool::InspectAsset(const FString& AssetPath, int32 MaxDepth,
	bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter,
	const FString& RowFilter) const
{
	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-asset inspect: path='%s'"), *AssetPath);

	// Try DataTable first
	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *AssetPath);
	if (DataTable)
	{
		TSharedPtr<FJsonObject> Result = InspectDataTable(DataTable, RowFilter);
		return FMcpToolResult::Json(Result);
	}

	// Try DataAsset
	UDataAsset* DataAsset = LoadObject<UDataAsset>(nullptr, *AssetPath);
	if (DataAsset)
	{
		TSharedPtr<FJsonObject> Result = InspectDataAsset(DataAsset);
		return FMcpToolResult::Json(Result);
	}

	// Try general UObject
	UObject* Object = LoadObject<UObject>(nullptr, *AssetPath);
	if (Object)
	{
		TSharedPtr<FJsonObject> Result = InspectObject(Object, MaxDepth, bIncludeDefaults, PropertyFilter, CategoryFilter);
		return FMcpToolResult::Json(Result);
	}

	return FMcpToolResult::Error(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectDataTable(UDataTable* DataTable, const FString& RowFilter) const
{
	if (!DataTable)
	{
		return nullptr;
	}

	TArray<FString> Warnings;
	TSharedPtr<FJsonObject> Result;
	if (!McpDataTableUtils::SerializeDataTable(DataTable, RowFilter, true, true, Result, Warnings) || !Result.IsValid())
	{
		return nullptr;
	}

	Result->SetStringField(TEXT("type"), TEXT("DataTable"));
	Result->SetStringField(TEXT("name"), DataTable->GetName());
	Result->SetStringField(TEXT("row_struct"), DataTable->GetRowStructPathName().ToString());

	const TArray<TSharedPtr<FJsonValue>>* RowsArray = nullptr;
	if (Result->TryGetArrayField(TEXT("rows"), RowsArray) && RowsArray)
	{
		for (const TSharedPtr<FJsonValue>& RowValue : *RowsArray)
		{
			const TSharedPtr<FJsonObject>* RowObject = nullptr;
			if (!RowValue.IsValid() || !RowValue->TryGetObject(RowObject) || !RowObject || !(*RowObject).IsValid())
			{
				continue;
			}

			FString RowName;
			if (!(*RowObject)->TryGetStringField(TEXT("row_name"), RowName))
			{
				continue;
			}

			(*RowObject)->SetStringField(TEXT("name"), RowName);

			if (const uint8* RowPtr = DataTable->FindRowUnchecked(FName(*RowName)))
			{
				if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
				{
					FString ExportText;
					RowStruct->ExportText(ExportText, RowPtr, nullptr, nullptr, PPF_None, nullptr);
					(*RowObject)->SetStringField(TEXT("data"), ExportText);
				}
			}
		}
	}

	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectDataAsset(UDataAsset* DataAsset) const
{
	if (!DataAsset)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("DataAsset"));
	Result->SetStringField(TEXT("name"), DataAsset->GetName());
	Result->SetStringField(TEXT("class"), DataAsset->GetClass()->GetName());

	// Get properties
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> PropIt(DataAsset->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(DataAsset);
		if (!ValuePtr) continue;

		TSharedPtr<FJsonObject> PropJson = PropertyToJson(Property, ValuePtr, DataAsset, 0, 2, false);
		if (PropJson.IsValid())
		{
			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropJson)));
		}
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> UQueryAssetTool::InspectObject(UObject* Object, int32 MaxDepth,
	bool bIncludeDefaults, const FString& PropertyFilter, const FString& CategoryFilter) const
{
	if (!Object)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("UObject"));
	Result->SetStringField(TEXT("name"), Object->GetName());
	Result->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("path"), Object->GetPathName());

	// Class hierarchy
	TArray<TSharedPtr<FJsonValue>> HierarchyArray;
	for (UClass* Class = Object->GetClass(); Class; Class = Class->GetSuperClass())
	{
		HierarchyArray.Add(MakeShareable(new FJsonValueString(Class->GetName())));
	}
	Result->SetArrayField(TEXT("class_hierarchy"), HierarchyArray);

	// Properties
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;

	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		// Apply filters
		if (!PropertyFilter.IsEmpty() && !MatchesWildcard(Property->GetName(), PropertyFilter))
		{
			continue;
		}

		if (!CategoryFilter.IsEmpty())
		{
			FString Category = Property->GetMetaData(TEXT("Category"));
			if (!MatchesWildcard(Category, CategoryFilter))
			{
				continue;
			}
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		if (!ValuePtr) continue;

		TSharedPtr<FJsonObject> PropJson = PropertyToJson(Property, ValuePtr, Object, 0, MaxDepth, bIncludeDefaults);
		if (PropJson.IsValid())
		{
			PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropJson)));
		}
	}

	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("property_count"), PropertiesArray.Num());

	return Result;
}

// === Helpers ===

TSharedPtr<FJsonObject> UQueryAssetTool::PropertyToJson(FProperty* Property, void* Container,
	UObject* Owner, int32 CurrentDepth, int32 MaxDepth, bool bIncludeDefaults) const
{
	if (!Property || !Container)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropJson = MakeShareable(new FJsonObject);
	PropJson->SetStringField(TEXT("name"), Property->GetName());
	PropJson->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

	FString Category = Property->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty())
	{
		PropJson->SetStringField(TEXT("category"), Category);
	}

	// Export value
	FString Value;
	Property->ExportText_Direct(Value, Container, Container, Owner, PPF_None);

	if (!bIncludeDefaults && Value.IsEmpty())
	{
		return nullptr;
	}

	PropJson->SetStringField(TEXT("value"), Value);

	return PropJson;
}

FString UQueryAssetTool::GetPropertyTypeString(FProperty* Property) const
{
	if (!Property) return TEXT("unknown");

	if (Property->IsA<FBoolProperty>()) return TEXT("bool");
	if (Property->IsA<FIntProperty>()) return TEXT("int32");
	if (Property->IsA<FFloatProperty>()) return TEXT("float");
	if (Property->IsA<FStrProperty>()) return TEXT("FString");
	if (Property->IsA<FNameProperty>()) return TEXT("FName");
	if (Property->IsA<FTextProperty>()) return TEXT("FText");

	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return StructProp->Struct ? StructProp->Struct->GetName() : TEXT("struct");
	}

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		return ObjProp->PropertyClass ? FString::Printf(TEXT("TObjectPtr<%s>"), *ObjProp->PropertyClass->GetName()) : TEXT("UObject*");
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeString(ArrayProp->Inner));
	}

	return Property->GetClass()->GetName();
}

bool UQueryAssetTool::MatchesWildcard(const FString& Name, const FString& Pattern) const
{
	if (Pattern.IsEmpty()) return true;

	if (Pattern.Contains(TEXT("*")))
	{
		if (Pattern.StartsWith(TEXT("*")) && Pattern.EndsWith(TEXT("*")))
		{
			return Name.Contains(Pattern.Mid(1, Pattern.Len() - 2));
		}
		if (Pattern.StartsWith(TEXT("*")))
		{
			return Name.EndsWith(Pattern.Mid(1));
		}
		if (Pattern.EndsWith(TEXT("*")))
		{
			return Name.StartsWith(Pattern.Left(Pattern.Len() - 1));
		}
	}

	return Name.Equals(Pattern, ESearchCase::IgnoreCase);
}
