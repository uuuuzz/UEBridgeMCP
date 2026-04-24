// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/References/FindReferencesTool.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CallFunction.h"
#include "FindInBlueprintManager.h"
#include "FindInBlueprints.h"
#include "ImaginaryBlueprintData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UEBridgeMCPEditor.h"

namespace
{
	bool AssetPathMatchesFilter(const FString& AssetObjectPath, const FString& PackageName, const FString& PathFilter)
	{
		return PathFilter.IsEmpty()
			|| AssetObjectPath.StartsWith(PathFilter, ESearchCase::IgnoreCase)
			|| PackageName.StartsWith(PathFilter, ESearchCase::IgnoreCase);
	}

	TArray<TSharedPtr<FJsonValue>> CollectAssetPackageResults(
		IAssetRegistry& AssetRegistry,
		const FName StartPackageName,
		bool bCollectReferencers,
		int32 MaxDepth,
		const FString& PathFilter,
		int32 Limit,
		bool& bOutTruncated)
	{
		bOutTruncated = false;

		FAssetRegistryDependencyOptions DependencyOptions;
		DependencyOptions.bIncludeHardPackageReferences = true;
		DependencyOptions.bIncludeSoftPackageReferences = true;
		DependencyOptions.bIncludeSearchableNames = false;
		DependencyOptions.bIncludeSoftManagementReferences = false;
		DependencyOptions.bIncludeHardManagementReferences = false;

		TArray<TTuple<FName, int32>> Queue;
		Queue.Add(MakeTuple(StartPackageName, 0));

		TSet<FName> VisitedPackages;
		VisitedPackages.Add(StartPackageName);

		TArray<TSharedPtr<FJsonValue>> Results;
		int32 QueueIndex = 0;
		while (QueueIndex < Queue.Num())
		{
			const TTuple<FName, int32>& Item = Queue[QueueIndex++];
			const FName CurrentPackage = Item.Get<0>();
			const int32 CurrentDepth = Item.Get<1>();
			if (CurrentDepth >= MaxDepth)
			{
				continue;
			}

			TArray<FName> RelatedPackages;
			if (bCollectReferencers)
			{
				AssetRegistry.K2_GetReferencers(CurrentPackage, DependencyOptions, RelatedPackages);
			}
			else
			{
				AssetRegistry.K2_GetDependencies(CurrentPackage, DependencyOptions, RelatedPackages);
			}

			for (const FName& RelatedPackage : RelatedPackages)
			{
				if (RelatedPackage.IsNone() || RelatedPackage == CurrentPackage)
				{
					continue;
				}

				const int32 RelatedDepth = CurrentDepth + 1;
				if (!VisitedPackages.Contains(RelatedPackage))
				{
					VisitedPackages.Add(RelatedPackage);
					Queue.Add(MakeTuple(RelatedPackage, RelatedDepth));
				}

				TArray<FAssetData> AssetsInPackage;
				AssetRegistry.GetAssetsByPackageName(RelatedPackage, AssetsInPackage);
				for (const FAssetData& Asset : AssetsInPackage)
				{
					const FString AssetObjectPath = Asset.GetObjectPathString();
					const FString PackageName = Asset.PackageName.ToString();
					if (!AssetPathMatchesFilter(AssetObjectPath, PackageName, PathFilter))
					{
						continue;
					}

					if (Results.Num() >= Limit)
					{
						bOutTruncated = true;
						return Results;
					}

					TSharedPtr<FJsonObject> RefObj = MakeShareable(new FJsonObject);
					RefObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					RefObj->SetStringField(TEXT("path"), AssetObjectPath);
					RefObj->SetStringField(TEXT("package"), PackageName);
					RefObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
					RefObj->SetNumberField(TEXT("depth"), RelatedDepth);
					RefObj->SetStringField(TEXT("relation"), bCollectReferencers ? TEXT("referencer") : TEXT("dependency"));
					Results.Add(MakeShareable(new FJsonValueObject(RefObj)));
				}
			}
		}

		return Results;
	}

	bool WriteJsonReport(const FString& OutputPath, const TSharedPtr<FJsonObject>& ResultObject, FString& OutResolvedPath, FString& OutError)
	{
		OutResolvedPath = FPaths::IsRelative(OutputPath)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OutputPath)
			: OutputPath;

		const FString Directory = FPaths::GetPath(OutResolvedPath);
		if (!Directory.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Directory, true);
		}

		FString SerializedJson;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedJson);
		if (!FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer))
		{
			OutError = TEXT("Failed to serialize references report");
			return false;
		}

		if (!FFileHelper::SaveStringToFile(SerializedJson, *OutResolvedPath))
		{
			OutError = FString::Printf(TEXT("Failed to write references report to '%s'"), *OutResolvedPath);
			return false;
		}

		return true;
	}
}

FString UFindReferencesTool::GetToolDescription() const
{
	return TEXT("Find references to assets, Blueprint variables, or nodes. "
		"Use type='asset' to find referencers, dependencies, or both for an asset, "
		"type='property' to find variable usages within a Blueprint, "
		"or type='node' to find node/function usages. "
		"asset_path can be a directory (e.g., /Game/Blueprints) to search all Blueprints recursively.");
}

TMap<FString, FMcpSchemaProperty> UFindReferencesTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty Type;
	Type.Type = TEXT("string");
	Type.Description = TEXT("Reference type: 'asset', 'property', or 'node'");
	Type.bRequired = true;
	Schema.Add(TEXT("type"), Type);

	FMcpSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to search from/within (e.g., /Game/Blueprints/BP_Character)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FMcpSchemaProperty VariableName;
	VariableName.Type = TEXT("string");
	VariableName.Description = TEXT("Variable name to find usages of (required for type='property')");
	VariableName.bRequired = false;
	Schema.Add(TEXT("variable_name"), VariableName);

	FMcpSchemaProperty NodeClass;
	NodeClass.Type = TEXT("string");
	NodeClass.Description = TEXT("Node class name to search for (e.g., K2Node_CallFunction). For type='node'");
	NodeClass.bRequired = false;
	Schema.Add(TEXT("node_class"), NodeClass);

	FMcpSchemaProperty FunctionName;
	FunctionName.Type = TEXT("string");
	FunctionName.Description = TEXT("Function name to filter CallFunction nodes. For type='node'");
	FunctionName.bRequired = false;
	Schema.Add(TEXT("function_name"), FunctionName);

	FMcpSchemaProperty Limit;
	Limit.Type = TEXT("integer");
	Limit.Description = TEXT("Maximum number of results to return (default: 100)");
	Limit.bRequired = false;
	Schema.Add(TEXT("limit"), Limit);

	FMcpSchemaProperty Direction;
	Direction.Type = TEXT("string");
	Direction.Description = TEXT("For type='asset': 'referencers', 'dependencies', or 'both' (default: referencers)");
	Direction.bRequired = false;
	Schema.Add(TEXT("direction"), Direction);

	FMcpSchemaProperty MaxDepth;
	MaxDepth.Type = TEXT("integer");
	MaxDepth.Description = TEXT("For type='asset': maximum traversal depth (default: 1)");
	MaxDepth.bRequired = false;
	Schema.Add(TEXT("max_depth"), MaxDepth);

	FMcpSchemaProperty PathFilterOut;
	PathFilterOut.Type = TEXT("string");
	PathFilterOut.Description = TEXT("Optional path prefix filter applied to returned asset results");
	PathFilterOut.bRequired = false;
	Schema.Add(TEXT("path_filter"), PathFilterOut);

	FMcpSchemaProperty OutputPath;
	OutputPath.Type = TEXT("string");
	OutputPath.Description = TEXT("Optional JSON report output path for asset reference results");
	OutputPath.bRequired = false;
	Schema.Add(TEXT("output_path"), OutputPath);

	return Schema;
}

TArray<FAssetData> UFindReferencesTool::GetBlueprintsInPath(const FString& Path) const
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Path));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> Blueprints;
	AssetRegistry.GetAssets(Filter, Blueprints);

	return Blueprints;
}

TArray<FString> UFindReferencesTool::GetRequiredParams() const
{
	return { TEXT("type"), TEXT("asset_path") };
}

FMcpToolResult UFindReferencesTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	// Get required parameters
	FString Type;
	if (!GetStringArg(Arguments, TEXT("type"), Type))
	{
		return FMcpToolResult::Error(TEXT("Missing required parameter: type"));
	}

	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	int32 Limit = GetIntArgOrDefault(Arguments, TEXT("limit"), 100);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("find-references: type='%s', path='%s', limit=%d"),
		*Type, *AssetPath, Limit);

	// Dispatch based on type
	if (Type == TEXT("asset"))
	{
		const FString Direction = GetStringArgOrDefault(Arguments, TEXT("direction"), TEXT("referencers"));
		const int32 MaxDepth = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("max_depth"), 1));
		const FString PathFilter = GetStringArgOrDefault(Arguments, TEXT("path_filter"), TEXT(""));
		const FString OutputPath = GetStringArgOrDefault(Arguments, TEXT("output_path"), TEXT(""));
		return FindAssetReferences(AssetPath, Direction, MaxDepth, PathFilter, OutputPath, Limit);
	}
	else if (Type == TEXT("property"))
	{
		FString VariableName;
		if (!GetStringArg(Arguments, TEXT("variable_name"), VariableName))
		{
			return FMcpToolResult::Error(TEXT("Parameter 'variable_name' is required when type is 'property'"));
		}
		return FindPropertyReferences(AssetPath, VariableName, Limit);
	}
	else if (Type == TEXT("node"))
	{
		FString NodeClass = GetStringArgOrDefault(Arguments, TEXT("node_class"), TEXT(""));
		FString FunctionName = GetStringArgOrDefault(Arguments, TEXT("function_name"), TEXT(""));

		if (NodeClass.IsEmpty() && FunctionName.IsEmpty())
		{
			return FMcpToolResult::Error(TEXT("At least one of 'node_class' or 'function_name' is required when type is 'node'"));
		}
		return FindNodeReferences(AssetPath, NodeClass, FunctionName, Limit);
	}
	else
	{
		return FMcpToolResult::Error(FString::Printf(
			TEXT("Invalid type '%s'. Must be 'asset', 'property', or 'node'"), *Type));
	}
}

FMcpToolResult UFindReferencesTool::FindAssetReferences(
	const FString& AssetPath,
	const FString& Direction,
	int32 MaxDepth,
	const FString& PathFilter,
	const FString& OutputPath,
	int32 Limit)
{
	FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
	if (PackagePath.IsEmpty())
	{
		PackagePath = AssetPath;
	}
	FName PackageName = FName(*PackagePath);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("asset"));
	Result->SetStringField(TEXT("target"), AssetPath);
	Result->SetStringField(TEXT("direction"), Direction);
	Result->SetNumberField(TEXT("max_depth"), MaxDepth);
	if (!PathFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("path_filter"), PathFilter);
	}

	if (!Direction.Equals(TEXT("referencers"), ESearchCase::IgnoreCase)
		&& !Direction.Equals(TEXT("dependencies"), ESearchCase::IgnoreCase)
		&& !Direction.Equals(TEXT("both"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Invalid direction '%s'"), *Direction));
	}

	bool bReferencersTruncated = false;
	bool bDependenciesTruncated = false;
	int32 TotalCount = 0;

	if (Direction.Equals(TEXT("referencers"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("both"), ESearchCase::IgnoreCase))
	{
		TArray<TSharedPtr<FJsonValue>> ReferencersArray = CollectAssetPackageResults(
			AssetRegistry,
			PackageName,
			true,
			MaxDepth,
			PathFilter,
			Limit,
			bReferencersTruncated);
		TotalCount += ReferencersArray.Num();
		Result->SetArrayField(TEXT("referencers"), ReferencersArray);
		Result->SetBoolField(TEXT("referencers_truncated"), bReferencersTruncated);
	}

	if (Direction.Equals(TEXT("dependencies"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("both"), ESearchCase::IgnoreCase))
	{
		TArray<TSharedPtr<FJsonValue>> DependenciesArray = CollectAssetPackageResults(
			AssetRegistry,
			PackageName,
			false,
			MaxDepth,
			PathFilter,
			Limit,
			bDependenciesTruncated);
		TotalCount += DependenciesArray.Num();
		Result->SetArrayField(TEXT("dependencies"), DependenciesArray);
		Result->SetBoolField(TEXT("dependencies_truncated"), bDependenciesTruncated);
	}

	Result->SetNumberField(TEXT("count"), TotalCount);
	Result->SetBoolField(TEXT("truncated"), bReferencersTruncated || bDependenciesTruncated);

	if (!OutputPath.IsEmpty())
	{
		FString ResolvedOutputPath;
		FString WriteError;
		if (!WriteJsonReport(OutputPath, Result, ResolvedOutputPath, WriteError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_IO_ERROR"), WriteError);
		}
		Result->SetStringField(TEXT("exported_report_path"), ResolvedOutputPath);
	}

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UFindReferencesTool::FindPropertyReferences(const FString& AssetPath, const FString& VariableName, int32 Limit)
{
	// Get Blueprints to search
	TArray<FAssetData> BlueprintsToSearch = GetBlueprintsInPath(AssetPath);

	// 蓝图全量加载数量上限，防止 GameThread 长时间阻塞
	constexpr int32 MaxBlueprintsToLoad = 200;
	bool bBlueprintsTruncated = false;
	if (BlueprintsToSearch.Num() > MaxBlueprintsToLoad)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("find-references: Path '%s' contains %d blueprints, truncating to %d to avoid long blocking"),
			*AssetPath, BlueprintsToSearch.Num(), MaxBlueprintsToLoad);
		bBlueprintsTruncated = true;
		BlueprintsToSearch.SetNum(MaxBlueprintsToLoad);
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("property"));
	Result->SetStringField(TEXT("search_path"), AssetPath);
	Result->SetStringField(TEXT("variable"), VariableName);

	TArray<TSharedPtr<FJsonValue>> UsagesArray;
	int32 GetCount = 0;
	int32 SetCount = 0;
	int32 BlueprintsSearched = 0;

	for (const FAssetData& AssetData : BlueprintsToSearch)
	{
		if (UsagesArray.Num() >= Limit)
		{
			break;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			continue;
		}

		BlueprintsSearched++;
		FString BlueprintPath = AssetData.GetObjectPathString();

		// Traverse all graphs
		TArray<UEdGraph*> AllGraphs = GetAllGraphs(Blueprint);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			if (UsagesArray.Num() >= Limit)
			{
				break;
			}

			FString GraphType = GetGraphType(Blueprint, Graph);

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				if (UsagesArray.Num() >= Limit)
				{
					break;
				}

				// Check if it's a variable node
				UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
				if (!VarNode)
				{
					continue;
				}

				FName VarRefName = VarNode->GetVarName();
				if (VarRefName.ToString() != VariableName)
				{
					continue;
				}

				// Determine access type
				bool bIsGetter = Cast<UK2Node_VariableGet>(Node) != nullptr;
				bool bIsSetter = Cast<UK2Node_VariableSet>(Node) != nullptr;

				FString AccessType = bIsGetter ? TEXT("get") : (bIsSetter ? TEXT("set") : TEXT("unknown"));

				if (bIsGetter)
				{
					GetCount++;
				}
				else if (bIsSetter)
				{
					SetCount++;
				}

				// Build usage entry
				TSharedPtr<FJsonObject> UsageObj = MakeShareable(new FJsonObject);
				UsageObj->SetStringField(TEXT("blueprint"), BlueprintPath);
				UsageObj->SetStringField(TEXT("graph"), Graph->GetName());
				UsageObj->SetStringField(TEXT("graph_type"), GraphType);
				UsageObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
				UsageObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
				UsageObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				UsageObj->SetStringField(TEXT("access_type"), AccessType);

				// Include position
				TSharedPtr<FJsonObject> PositionObj = MakeShareable(new FJsonObject);
				PositionObj->SetNumberField(TEXT("x"), Node->NodePosX);
				PositionObj->SetNumberField(TEXT("y"), Node->NodePosY);
				UsageObj->SetObjectField(TEXT("position"), PositionObj);

				UsagesArray.Add(MakeShareable(new FJsonValueObject(UsageObj)));
			}
		}
	}

	Result->SetArrayField(TEXT("usages"), UsagesArray);
	Result->SetNumberField(TEXT("count"), UsagesArray.Num());
	Result->SetNumberField(TEXT("get_count"), GetCount);
	Result->SetNumberField(TEXT("set_count"), SetCount);
	Result->SetNumberField(TEXT("blueprints_searched"), BlueprintsSearched);
	Result->SetBoolField(TEXT("truncated"), UsagesArray.Num() >= Limit);

	return FMcpToolResult::Json(Result);
}

TArray<FSoftObjectPath> UFindReferencesTool::FindMatchingBlueprintsViaFiB(const FString& SearchTerm, const FString& PathFilter)
{
	TArray<FSoftObjectPath> MatchingBlueprints;

	// Create a synchronous search using FStreamSearch
	FStreamSearchOptions SearchOptions;
	SearchOptions.ImaginaryDataFilter = ESearchQueryFilter::NodesFilter; // Focus on nodes
	TSharedRef<FStreamSearch> StreamSearch = MakeShared<FStreamSearch>(SearchTerm, SearchOptions);

	// Run the search synchronously
	StreamSearch->Init();
	StreamSearch->Run();
	StreamSearch->EnsureCompletion();

	// Get search results (these include Blueprint paths)
	TArray<TSharedPtr<FFindInBlueprintsResult>> Results;
	StreamSearch->GetFilteredItems(Results);

	for (const TSharedPtr<FFindInBlueprintsResult>& Result : Results)
	{
		if (!Result.IsValid())
		{
			continue;
		}

		// GetParentBlueprint() will load the Blueprint, but we can check the display text
		// to get the path without loading. The root result's display text contains the BP name.
		// However, for safety we'll use GetParentBlueprint since that's the reliable method.
		UBlueprint* BP = Result->GetParentBlueprint();
		if (BP)
		{
			FSoftObjectPath AssetPath(BP);
			FString PathString = AssetPath.GetAssetPathString();

			// Apply path filter if specified
			if (!PathFilter.IsEmpty() && !PathString.StartsWith(PathFilter))
			{
				continue;
			}

			MatchingBlueprints.AddUnique(AssetPath);
		}
	}

	return MatchingBlueprints;
}

FMcpToolResult UFindReferencesTool::FindNodeReferences(const FString& AssetPath, const FString& NodeClass,
													   const FString& FunctionName, int32 Limit)
{
	// Check FiB cache status
	FFindInBlueprintSearchManager& SearchManager = FFindInBlueprintSearchManager::Get();
	bool bCacheInProgress = SearchManager.IsCacheInProgress();
	bool bDiscoveryInProgress = SearchManager.IsAssetDiscoveryInProgress();
	int32 UnindexedCount = SearchManager.GetNumberUnindexedAssets();

	// Build the search term for FiB
	FString SearchTerm;
	if (!NodeClass.IsEmpty())
	{
		SearchTerm = NodeClass;
	}
	if (!FunctionName.IsEmpty())
	{
		if (!SearchTerm.IsEmpty())
		{
			SearchTerm += TEXT(" ");
		}
		SearchTerm += FunctionName;
	}

	// Only use FiB cache if indexing is complete
	TArray<FSoftObjectPath> FiBMatches;
	bool bUsedFiBCache = false;
	if (!bCacheInProgress && !bDiscoveryInProgress && UnindexedCount == 0)
	{
		FiBMatches = FindMatchingBlueprintsViaFiB(SearchTerm, AssetPath);
		bUsedFiBCache = FiBMatches.Num() > 0;
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("node"));
	Result->SetStringField(TEXT("search_path"), AssetPath);
	Result->SetBoolField(TEXT("used_fib_cache"), bUsedFiBCache);

	// Add FiB cache status
	TSharedPtr<FJsonObject> CacheStatusObj = MakeShareable(new FJsonObject);
	CacheStatusObj->SetBoolField(TEXT("cache_in_progress"), bCacheInProgress);
	CacheStatusObj->SetBoolField(TEXT("discovery_in_progress"), bDiscoveryInProgress);
	CacheStatusObj->SetNumberField(TEXT("unindexed_count"), UnindexedCount);
	CacheStatusObj->SetBoolField(TEXT("cache_ready"), !bCacheInProgress && !bDiscoveryInProgress && UnindexedCount == 0);
	Result->SetObjectField(TEXT("fib_cache_status"), CacheStatusObj);

	// Add search criteria
	TSharedPtr<FJsonObject> SearchObj = MakeShareable(new FJsonObject);
	if (!NodeClass.IsEmpty())
	{
		SearchObj->SetStringField(TEXT("node_class"), NodeClass);
	}
	if (!FunctionName.IsEmpty())
	{
		SearchObj->SetStringField(TEXT("function_name"), FunctionName);
	}
	Result->SetObjectField(TEXT("search"), SearchObj);

	TArray<TSharedPtr<FJsonValue>> UsagesArray;
	int32 BlueprintsSearched = 0;

	// Determine which blueprints to search
	TArray<FAssetData> BlueprintsToSearch;

	if (bUsedFiBCache)
	{
		// Use FiB-filtered results - only load matching Blueprints
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		for (const FSoftObjectPath& Path : FiBMatches)
		{
			FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(Path);
			if (AssetData.IsValid())
			{
				BlueprintsToSearch.Add(AssetData);
			}
		}
		Result->SetNumberField(TEXT("fib_matches"), FiBMatches.Num());
	}
	else
	{
		// Fallback to loading all Blueprints in path (when cache not ready or no matches)
		BlueprintsToSearch = GetBlueprintsInPath(AssetPath);
	}

	// 蓝图全量加载数量上限，防止 GameThread 长时间阻塞
	constexpr int32 MaxBlueprintsToLoad = 200;
	bool bBlueprintsTruncated = false;
	if (BlueprintsToSearch.Num() > MaxBlueprintsToLoad)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("find-references: Path '%s' contains %d blueprints, truncating to %d to avoid long blocking"),
			*AssetPath, BlueprintsToSearch.Num(), MaxBlueprintsToLoad);
		bBlueprintsTruncated = true;
		BlueprintsToSearch.SetNum(MaxBlueprintsToLoad);
	}

	for (const FAssetData& AssetData : BlueprintsToSearch)
	{
		if (UsagesArray.Num() >= Limit)
		{
			break;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			continue;
		}

		BlueprintsSearched++;
		FString BlueprintPath = AssetData.GetObjectPathString();

		// Traverse all graphs
		TArray<UEdGraph*> AllGraphs = GetAllGraphs(Blueprint);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			if (UsagesArray.Num() >= Limit)
			{
				break;
			}

			FString GraphType = GetGraphType(Blueprint, Graph);

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				if (UsagesArray.Num() >= Limit)
				{
					break;
				}

				bool bMatches = false;
				FString MatchedFunctionName;

				// Match by node class
				if (!NodeClass.IsEmpty())
				{
					FString ActualClass = Node->GetClass()->GetName();
					if (ActualClass == NodeClass || ActualClass.Contains(NodeClass))
					{
						bMatches = true;
					}
				}

				// Match by function name (for CallFunction nodes)
				if (!FunctionName.IsEmpty())
				{
					UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
					if (CallNode)
					{
						UFunction* Func = CallNode->GetTargetFunction();
						if (Func && Func->GetName().Contains(FunctionName))
						{
							bMatches = true;
							MatchedFunctionName = Func->GetName();
						}
					}
				}

				if (!bMatches)
				{
					continue;
				}

				// Build usage entry
				TSharedPtr<FJsonObject> UsageObj = NodeReferenceToJson(Node, Graph, GraphType);
				UsageObj->SetStringField(TEXT("blueprint"), BlueprintPath);

				// Add function name if matched
				if (!MatchedFunctionName.IsEmpty())
				{
					UsageObj->SetStringField(TEXT("function"), MatchedFunctionName);
				}

				// For CallFunction nodes, add target class
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (CallNode)
				{
					UFunction* Func = CallNode->GetTargetFunction();
					if (Func && Func->GetOwnerClass())
					{
						UsageObj->SetStringField(TEXT("target_class"), Func->GetOwnerClass()->GetName());
					}
				}

				UsagesArray.Add(MakeShareable(new FJsonValueObject(UsageObj)));
			}
		}
	}

	Result->SetArrayField(TEXT("usages"), UsagesArray);
	Result->SetNumberField(TEXT("count"), UsagesArray.Num());
	Result->SetNumberField(TEXT("blueprints_searched"), BlueprintsSearched);
	Result->SetBoolField(TEXT("truncated"), UsagesArray.Num() >= Limit);
	if (bBlueprintsTruncated)
	{
		Result->SetBoolField(TEXT("blueprints_truncated"), true);
		Result->SetNumberField(TEXT("max_blueprints_to_load"), MaxBlueprintsToLoad);
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Search path contains too many blueprints. Only the first %d were loaded. Use a more specific asset_path to narrow results."), MaxBlueprintsToLoad));
	}

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UFindReferencesTool::FindNodeReferencesLegacy(const FString& AssetPath, const FString& NodeClass,
														   const FString& FunctionName, int32 Limit)
{
	// Get Blueprints to search
	TArray<FAssetData> BlueprintsToSearch = GetBlueprintsInPath(AssetPath);

	// 蓝图全量加载数量上限，防止 GameThread 长时间阻塞
	constexpr int32 MaxBlueprintsToLoadLegacy = 200;
	bool bBlueprintsTruncatedLegacy = false;
	if (BlueprintsToSearch.Num() > MaxBlueprintsToLoadLegacy)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("find-references: Path '%s' contains %d blueprints, truncating to %d to avoid long blocking"),
			*AssetPath, BlueprintsToSearch.Num(), MaxBlueprintsToLoadLegacy);
		bBlueprintsTruncatedLegacy = true;
		BlueprintsToSearch.SetNum(MaxBlueprintsToLoadLegacy);
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("type"), TEXT("node"));
	Result->SetStringField(TEXT("search_path"), AssetPath);

	// Add search criteria
	TSharedPtr<FJsonObject> SearchObj = MakeShareable(new FJsonObject);
	if (!NodeClass.IsEmpty())
	{
		SearchObj->SetStringField(TEXT("node_class"), NodeClass);
	}
	if (!FunctionName.IsEmpty())
	{
		SearchObj->SetStringField(TEXT("function_name"), FunctionName);
	}
	Result->SetObjectField(TEXT("search"), SearchObj);

	TArray<TSharedPtr<FJsonValue>> UsagesArray;
	int32 BlueprintsSearched = 0;

	for (const FAssetData& AssetData : BlueprintsToSearch)
	{
		if (UsagesArray.Num() >= Limit)
		{
			break;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			continue;
		}

		BlueprintsSearched++;
		FString BlueprintPath = AssetData.GetObjectPathString();

		// Traverse all graphs
		TArray<UEdGraph*> AllGraphs = GetAllGraphs(Blueprint);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			if (UsagesArray.Num() >= Limit)
			{
				break;
			}

			FString GraphType = GetGraphType(Blueprint, Graph);

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				if (UsagesArray.Num() >= Limit)
				{
					break;
				}

				bool bMatches = false;
				FString MatchedFunctionName;

				// Match by node class
				if (!NodeClass.IsEmpty())
				{
					FString ActualClass = Node->GetClass()->GetName();
					if (ActualClass == NodeClass || ActualClass.Contains(NodeClass))
					{
						bMatches = true;
					}
				}

				// Match by function name (for CallFunction nodes)
				if (!FunctionName.IsEmpty())
				{
					UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
					if (CallNode)
					{
						UFunction* Func = CallNode->GetTargetFunction();
						if (Func && Func->GetName().Contains(FunctionName))
						{
							bMatches = true;
							MatchedFunctionName = Func->GetName();
						}
					}
				}

				if (!bMatches)
				{
					continue;
				}

				// Build usage entry
				TSharedPtr<FJsonObject> UsageObj = NodeReferenceToJson(Node, Graph, GraphType);
				UsageObj->SetStringField(TEXT("blueprint"), BlueprintPath);

				// Add function name if matched
				if (!MatchedFunctionName.IsEmpty())
				{
					UsageObj->SetStringField(TEXT("function"), MatchedFunctionName);
				}

				// For CallFunction nodes, add target class
				UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (CallNode)
				{
					UFunction* Func = CallNode->GetTargetFunction();
					if (Func && Func->GetOwnerClass())
					{
						UsageObj->SetStringField(TEXT("target_class"), Func->GetOwnerClass()->GetName());
					}
				}

				UsagesArray.Add(MakeShareable(new FJsonValueObject(UsageObj)));
			}
		}
	}

	Result->SetArrayField(TEXT("usages"), UsagesArray);
	Result->SetNumberField(TEXT("count"), UsagesArray.Num());
	Result->SetNumberField(TEXT("blueprints_searched"), BlueprintsSearched);
	Result->SetBoolField(TEXT("truncated"), UsagesArray.Num() >= Limit);
	if (bBlueprintsTruncatedLegacy)
	{
		Result->SetBoolField(TEXT("blueprints_truncated"), true);
		Result->SetNumberField(TEXT("max_blueprints_to_load"), MaxBlueprintsToLoadLegacy);
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Search path contains too many blueprints. Only the first %d were loaded. Use a more specific asset_path to narrow results."), MaxBlueprintsToLoadLegacy));
	}

	return FMcpToolResult::Json(Result);
}

TArray<UEdGraph*> UFindReferencesTool::GetAllGraphs(UBlueprint* Blueprint) const
{
	TArray<UEdGraph*> AllGraphs;

	if (!Blueprint)
	{
		return AllGraphs;
	}

	AllGraphs.Append(Blueprint->UbergraphPages);
	AllGraphs.Append(Blueprint->FunctionGraphs);
	AllGraphs.Append(Blueprint->MacroGraphs);

	return AllGraphs;
}

FString UFindReferencesTool::GetGraphType(UBlueprint* Blueprint, UEdGraph* Graph) const
{
	if (!Blueprint || !Graph)
	{
		return TEXT("unknown");
	}

	if (Blueprint->UbergraphPages.Contains(Graph))
	{
		return TEXT("event");
	}
	else if (Blueprint->FunctionGraphs.Contains(Graph))
	{
		return TEXT("function");
	}
	else if (Blueprint->MacroGraphs.Contains(Graph))
	{
		return TEXT("macro");
	}

	return TEXT("unknown");
}

TSharedPtr<FJsonObject> UFindReferencesTool::NodeReferenceToJson(UEdGraphNode* Node, UEdGraph* Graph,
																  const FString& GraphType) const
{
	TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);

	if (!Node || !Graph)
	{
		return NodeObj;
	}

	NodeObj->SetStringField(TEXT("graph"), Graph->GetName());
	NodeObj->SetStringField(TEXT("graph_type"), GraphType);
	NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	// Include position
	TSharedPtr<FJsonObject> PositionObj = MakeShareable(new FJsonObject);
	PositionObj->SetNumberField(TEXT("x"), Node->NodePosX);
	PositionObj->SetNumberField(TEXT("y"), Node->NodePosY);
	NodeObj->SetObjectField(TEXT("position"), PositionObj);

	return NodeObj;
}
