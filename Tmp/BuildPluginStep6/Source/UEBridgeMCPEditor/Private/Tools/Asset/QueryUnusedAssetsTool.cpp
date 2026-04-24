// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/QueryUnusedAssetsTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "UObject/ObjectRedirector.h"
#include "Utils/McpPropertySerializer.h"

namespace
{
	bool PathStartsWithAny(const FString& AssetPath, const TArray<FString>& Prefixes)
	{
		for (const FString& Prefix : Prefixes)
		{
			if (!Prefix.IsEmpty() && AssetPath.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool IsPrimaryAsset(const FAssetData& AssetData)
	{
		if (AssetData.GetPrimaryAssetId().IsValid())
		{
			return true;
		}

		if (UAssetManager* AssetManager = UAssetManager::GetIfInitialized())
		{
			return AssetManager->GetPrimaryAssetIdForData(AssetData).IsValid();
		}

		return false;
	}
}

FString UQueryUnusedAssetsTool::GetToolDescription() const
{
	return TEXT("Find conservative unused-asset candidates by scanning for assets with no hard or soft referencers.");
}

TMap<FString, FMcpSchemaProperty> UQueryUnusedAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Root path to scan. Defaults to /Game")));
	Schema.Add(TEXT("class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional asset class filter")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum number of candidates to return")));
	Schema.Add(TEXT("exclude_paths"), FMcpSchemaProperty::MakeArray(TEXT("Additional path prefixes to exclude"), TEXT("string")));
	return Schema;
}

TArray<FString> UQueryUnusedAssetsTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UQueryUnusedAssetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RootPath = GetStringArgOrDefault(Arguments, TEXT("path"), TEXT("/Game"));
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class"));
	const int32 Limit = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("limit"), 100));

	TArray<FString> ExcludedPaths = { TEXT("/Game/Developers") };
	const TArray<TSharedPtr<FJsonValue>>* ExcludedPathArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("exclude_paths"), ExcludedPathArray) && ExcludedPathArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ExcludedPathArray)
		{
			if (Value.IsValid())
			{
				ExcludedPaths.Add(Value->AsString());
			}
		}
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*RootPath));
	Filter.bRecursivePaths = true;

	UClass* ResolvedClass = nullptr;
	if (!ClassFilter.IsEmpty())
	{
		FString ClassError;
		ResolvedClass = FMcpPropertySerializer::ResolveClass(ClassFilter, ClassError);
		if (ResolvedClass)
		{
			Filter.ClassPaths.Add(ResolvedClass->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	FAssetRegistryDependencyOptions ReferenceOptions;
	ReferenceOptions.bIncludeHardPackageReferences = true;
	ReferenceOptions.bIncludeSoftPackageReferences = true;
	ReferenceOptions.bIncludeSearchableNames = false;
	ReferenceOptions.bIncludeSoftManagementReferences = false;
	ReferenceOptions.bIncludeHardManagementReferences = false;

	TArray<TSharedPtr<FJsonValue>> CandidateArray;
	TArray<TSharedPtr<FJsonValue>> WarningArray;
	bool bTruncated = false;

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (!ClassFilter.IsEmpty() && !ResolvedClass && !AssetData.AssetClassPath.GetAssetName().ToString().Equals(ClassFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString ObjectPath = AssetData.GetObjectPathString();
		if (PathStartsWithAny(ObjectPath, ExcludedPaths))
		{
			continue;
		}

		if (AssetData.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
		{
			continue;
		}
		if (AssetData.AssetClassPath == UObjectRedirector::StaticClass()->GetClassPathName())
		{
			continue;
		}
		if (IsPrimaryAsset(AssetData))
		{
			continue;
		}

		TArray<FName> ReferencerPackages;
		AssetRegistry.K2_GetReferencers(AssetData.PackageName, ReferenceOptions, ReferencerPackages);
		ReferencerPackages.Remove(AssetData.PackageName);
		if (ReferencerPackages.Num() > 0)
		{
			continue;
		}

		TSharedPtr<FJsonObject> CandidateObject = MakeShareable(new FJsonObject);
		CandidateObject->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		CandidateObject->SetStringField(TEXT("path"), ObjectPath);
		CandidateObject->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
		CandidateObject->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		CandidateObject->SetStringField(TEXT("reason"), TEXT("No hard or soft referencers were found in the asset registry"));

		if (CandidateArray.Num() >= Limit)
		{
			bTruncated = true;
			break;
		}
		CandidateArray.Add(MakeShareable(new FJsonValueObject(CandidateObject)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("path"), RootPath);
	Result->SetArrayField(TEXT("candidates"), CandidateArray);
	Result->SetArrayField(TEXT("warnings"), WarningArray);
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	Result->SetNumberField(TEXT("count"), CandidateArray.Num());
	return FMcpToolResult::StructuredJson(Result);
}
