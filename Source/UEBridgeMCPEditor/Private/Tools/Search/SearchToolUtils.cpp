// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Search/SearchToolUtils.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/McpToolBase.h"
#include "Utils/McpPropertySerializer.h"
#include "Utils/McpV2ToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace
{
	bool ContainsAnyWildcard(const FString& Value)
	{
		return Value.Contains(TEXT("*")) || Value.Contains(TEXT("?"));
	}

	FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		FString Result = PinType.PinCategory.ToString();
		if (!PinType.PinSubCategory.IsNone())
		{
			Result += TEXT(".");
			Result += PinType.PinSubCategory.ToString();
		}
		if (PinType.PinSubCategoryObject.IsValid())
		{
			Result += TEXT(" ");
			Result += PinType.PinSubCategoryObject->GetPathName();
		}
		if (PinType.ContainerType == EPinContainerType::Array)
		{
			Result = TEXT("array<") + Result + TEXT(">");
		}
		else if (PinType.ContainerType == EPinContainerType::Set)
		{
			Result = TEXT("set<") + Result + TEXT(">");
		}
		else if (PinType.ContainerType == EPinContainerType::Map)
		{
			Result = TEXT("map<") + Result + TEXT(">");
		}
		return Result;
	}

	bool TypeAllowed(const TSet<FString>& SymbolTypes, const FString& Type)
	{
		return SymbolTypes.Num() == 0 || SymbolTypes.Contains(Type);
	}

	void AddBlueprintSymbol(
		TArray<SearchToolUtils::FSearchItem>& OutItems,
		const FString& Query,
		const FString& AssetPath,
		const FString& BlueprintName,
		const FString& SymbolType,
		const FString& Name,
		const FString& GraphName,
		const FString& GraphType,
		const FString& Detail,
		double BaseScore)
	{
		const double Score = BaseScore + SearchToolUtils::ScoreFields({ Name, GraphName, GraphType, Detail, BlueprintName, AssetPath }, Query);
		if (!Query.IsEmpty() && Score <= 0.0)
		{
			return;
		}

		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("kind"), TEXT("blueprint_symbol"));
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("blueprint"), BlueprintName);
		Object->SetStringField(TEXT("symbol_type"), SymbolType);
		Object->SetStringField(TEXT("name"), Name);
		Object->SetNumberField(TEXT("score"), Score);
		if (!GraphName.IsEmpty())
		{
			Object->SetStringField(TEXT("graph"), GraphName);
		}
		if (!GraphType.IsEmpty())
		{
			Object->SetStringField(TEXT("graph_type"), GraphType);
		}
		if (!Detail.IsEmpty())
		{
			Object->SetStringField(TEXT("detail"), Detail);
		}

		SearchToolUtils::FSearchItem Item;
		Item.Score = Score;
		Item.Object = Object;
		OutItems.Add(Item);
	}
}

namespace SearchToolUtils
{
	FString NormalizeToken(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT(""));
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ToLowerInline();
		return Value;
	}

	FString BuildCamelCaseAcronym(const FString& Value)
	{
		FString Acronym;
		bool bPreviousWasSeparator = true;
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			if (!FChar::IsAlnum(Char))
			{
				bPreviousWasSeparator = true;
				continue;
			}

			if (bPreviousWasSeparator || FChar::IsUpper(Char))
			{
				Acronym.AppendChar(FChar::ToLower(Char));
			}
			bPreviousWasSeparator = false;
		}
		return Acronym;
	}

	double ScoreText(const FString& Value, const FString& Query)
	{
		if (Query.IsEmpty())
		{
			return 1.0;
		}
		if (Value.IsEmpty())
		{
			return 0.0;
		}

		if (ContainsAnyWildcard(Query))
		{
			return Value.MatchesWildcard(Query, ESearchCase::IgnoreCase) ? 80.0 : 0.0;
		}

		const FString NormalizedValue = NormalizeToken(Value);
		const FString NormalizedQuery = NormalizeToken(Query);
		if (NormalizedValue.IsEmpty() || NormalizedQuery.IsEmpty())
		{
			return 0.0;
		}

		if (NormalizedValue.Equals(NormalizedQuery, ESearchCase::IgnoreCase))
		{
			return 100.0;
		}
		if (NormalizedValue.StartsWith(NormalizedQuery, ESearchCase::IgnoreCase))
		{
			return 85.0;
		}
		if (NormalizedValue.Contains(NormalizedQuery, ESearchCase::IgnoreCase))
		{
			return 65.0;
		}

		const FString Acronym = BuildCamelCaseAcronym(Value);
		if (!Acronym.IsEmpty())
		{
			if (Acronym.Equals(NormalizedQuery, ESearchCase::IgnoreCase))
			{
				return 75.0;
			}
			if (Acronym.StartsWith(NormalizedQuery, ESearchCase::IgnoreCase))
			{
				return 55.0;
			}
		}

		int32 QueryIndex = 0;
		for (int32 ValueIndex = 0; ValueIndex < NormalizedValue.Len() && QueryIndex < NormalizedQuery.Len(); ++ValueIndex)
		{
			if (NormalizedValue[ValueIndex] == NormalizedQuery[QueryIndex])
			{
				++QueryIndex;
			}
		}
		if (QueryIndex == NormalizedQuery.Len())
		{
			return 35.0;
		}

		return 0.0;
	}

	double ScoreFields(const TArray<FString>& Fields, const FString& Query)
	{
		double BestScore = Query.IsEmpty() ? 1.0 : 0.0;
		for (int32 Index = 0; Index < Fields.Num(); ++Index)
		{
			const double FieldScore = ScoreText(Fields[Index], Query);
			const double WeightedScore = FieldScore - static_cast<double>(Index) * 2.0;
			BestScore = FMath::Max(BestScore, WeightedScore);
		}
		return BestScore;
	}

	bool MatchesQuery(const TArray<FString>& Fields, const FString& Query)
	{
		return Query.IsEmpty() || ScoreFields(Fields, Query) > 0.0;
	}

	void SortAndTrim(TArray<FSearchItem>& Items, int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		Items.Sort([](const FSearchItem& A, const FSearchItem& B)
		{
			return A.Score > B.Score;
		});

		const int32 MaxItems = FMath::Max(1, Limit);
		for (int32 Index = 0; Index < Items.Num() && Index < MaxItems; ++Index)
		{
			if (Items[Index].Object.IsValid())
			{
				OutArray.Add(MakeShareable(new FJsonValueObject(Items[Index].Object)));
			}
		}
	}

	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, ArrayField) || !ArrayField)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *ArrayField)
		{
			if (Value.IsValid())
			{
				const FString StringValue = Value->AsString();
				if (!StringValue.IsEmpty())
				{
					OutValues.Add(StringValue);
				}
			}
		}
	}

	TArray<FString> ReadPathFilters(const TSharedPtr<FJsonObject>& Arguments, const FString& DefaultPath)
	{
		TArray<FString> Paths;
		ExtractStringArrayField(Arguments, TEXT("paths"), Paths);
		FString SinglePath;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("path"), SinglePath);
		}
		if (!SinglePath.IsEmpty())
		{
			Paths.Add(SinglePath);
		}
		if (Paths.Num() == 0 && !DefaultPath.IsEmpty())
		{
			Paths.Add(DefaultPath);
		}
		return Paths;
	}

	bool PathMatchesAny(const FString& Value, const TArray<FString>& PathFilters)
	{
		if (PathFilters.Num() == 0)
		{
			return true;
		}
		for (const FString& Path : PathFilters)
		{
			if (!Path.IsEmpty() && Value.StartsWith(Path, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonObject> SerializeAssetData(const FAssetData& AssetData, const FString& Query, double Score, const FString& Kind)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("kind"), Kind);
		Object->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Object->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		Object->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
		Object->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		Object->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		Object->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
		Object->SetNumberField(TEXT("score"), Score);
		return Object;
	}

	void CollectAssetResults(
		const FString& Query,
		const TArray<FString>& Paths,
		const FString& ClassFilter,
		bool bIncludeOnlyOnDiskAssets,
		int32 Limit,
		TArray<FSearchItem>& OutItems,
		int32& OutScanned,
		bool& bOutClassFilterResolved)
	{
		OutScanned = 0;
		bOutClassFilterResolved = false;

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = bIncludeOnlyOnDiskAssets;
		for (const FString& Path : Paths)
		{
			if (!Path.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*Path));
			}
		}

		UClass* ResolvedClass = nullptr;
		if (!ClassFilter.IsEmpty())
		{
			FString ClassError;
			ResolvedClass = FMcpPropertySerializer::ResolveClass(ClassFilter, ClassError);
			if (ResolvedClass)
			{
				Filter.ClassPaths.Add(ResolvedClass->GetClassPathName());
				Filter.bRecursiveClasses = true;
				bOutClassFilterResolved = true;
			}
		}

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		for (const FAssetData& AssetData : Assets)
		{
			++OutScanned;
			if (!ClassFilter.IsEmpty() && !ResolvedClass)
			{
				const FString ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
				const FString ClassPath = AssetData.AssetClassPath.ToString();
				if (!ClassName.Equals(ClassFilter, ESearchCase::IgnoreCase)
					&& !ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase)
					&& !ClassPath.Contains(ClassFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}
			}

			const TArray<FString> Fields = {
				AssetData.AssetName.ToString(),
				AssetData.GetObjectPathString(),
				AssetData.PackageName.ToString(),
				AssetData.AssetClassPath.GetAssetName().ToString(),
				AssetData.AssetClassPath.ToString()
			};
			const double Score = ScoreFields(Fields, Query);
			if (!Query.IsEmpty() && Score <= 0.0)
			{
				continue;
			}

			FSearchItem Item;
			Item.Score = Score;
			Item.Object = SerializeAssetData(AssetData, Query, Score);
			OutItems.Add(Item);
		}
	}

	void CollectBlueprintSymbolResults(
		const FString& Query,
		const TArray<FString>& Paths,
		const TSet<FString>& SymbolTypes,
		bool bIncludeNodes,
		int32 MaxBlueprints,
		int32 Limit,
		TArray<FSearchItem>& OutItems,
		int32& OutBlueprintsScanned,
		bool& bOutBlueprintsTruncated)
	{
		OutBlueprintsScanned = 0;
		bOutBlueprintsTruncated = false;

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		for (const FString& Path : Paths)
		{
			if (!Path.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*Path));
			}
		}

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Blueprints;
		AssetRegistry.GetAssets(Filter, Blueprints);

		const int32 ClampedMaxBlueprints = FMath::Max(1, MaxBlueprints);
		if (Blueprints.Num() > ClampedMaxBlueprints)
		{
			Blueprints.SetNum(ClampedMaxBlueprints);
			bOutBlueprintsTruncated = true;
		}

		for (const FAssetData& AssetData : Blueprints)
		{
			if (OutItems.Num() >= Limit * 4)
			{
				break;
			}

			UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
			if (!Blueprint)
			{
				continue;
			}
			++OutBlueprintsScanned;

			const FString AssetPath = AssetData.GetObjectPathString();
			const FString BlueprintName = Blueprint->GetName();

			if (TypeAllowed(SymbolTypes, TEXT("variable")))
			{
				for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
				{
					AddBlueprintSymbol(
						OutItems,
						Query,
						AssetPath,
						BlueprintName,
						TEXT("variable"),
						Variable.VarName.ToString(),
						FString(),
						FString(),
						PinTypeToString(Variable.VarType),
						15.0);
				}
			}

			if (TypeAllowed(SymbolTypes, TEXT("function")))
			{
				for (UEdGraph* Graph : Blueprint->FunctionGraphs)
				{
					if (Graph)
					{
						AddBlueprintSymbol(OutItems, Query, AssetPath, BlueprintName, TEXT("function"), Graph->GetName(), Graph->GetName(), TEXT("function"), FString(), 12.0);
					}
				}
			}

			if (TypeAllowed(SymbolTypes, TEXT("macro")))
			{
				for (UEdGraph* Graph : Blueprint->MacroGraphs)
				{
					if (Graph)
					{
						AddBlueprintSymbol(OutItems, Query, AssetPath, BlueprintName, TEXT("macro"), Graph->GetName(), Graph->GetName(), TEXT("macro"), FString(), 12.0);
					}
				}
			}

			if (TypeAllowed(SymbolTypes, TEXT("graph")))
			{
				for (UEdGraph* Graph : Blueprint->UbergraphPages)
				{
					if (Graph)
					{
						AddBlueprintSymbol(OutItems, Query, AssetPath, BlueprintName, TEXT("graph"), Graph->GetName(), Graph->GetName(), TEXT("event"), FString(), 8.0);
					}
				}
			}

			if (bIncludeNodes && TypeAllowed(SymbolTypes, TEXT("node")))
			{
				TArray<UEdGraph*> Graphs;
				Graphs.Append(Blueprint->UbergraphPages);
				Graphs.Append(Blueprint->FunctionGraphs);
				Graphs.Append(Blueprint->MacroGraphs);
				for (UEdGraph* Graph : Graphs)
				{
					if (!Graph)
					{
						continue;
					}
					const FString GraphType = McpV2ToolUtils::GetBlueprintGraphType(Blueprint, Graph);
					for (UEdGraphNode* Node : Graph->Nodes)
					{
						if (!Node)
						{
							continue;
						}
						const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
						const FString NodeClass = Node->GetClass()->GetName();
						const double Score = 5.0 + ScoreFields({ NodeTitle, NodeClass, Graph->GetName(), BlueprintName, AssetPath }, Query);
						if (!Query.IsEmpty() && Score <= 5.0)
						{
							continue;
						}
						TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
						Object->SetStringField(TEXT("kind"), TEXT("blueprint_symbol"));
						Object->SetStringField(TEXT("asset_path"), AssetPath);
						Object->SetStringField(TEXT("blueprint"), BlueprintName);
						Object->SetStringField(TEXT("symbol_type"), TEXT("node"));
						Object->SetStringField(TEXT("name"), NodeTitle);
						Object->SetStringField(TEXT("node_class"), NodeClass);
						Object->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
						Object->SetStringField(TEXT("graph"), Graph->GetName());
						Object->SetStringField(TEXT("graph_type"), GraphType);
						Object->SetNumberField(TEXT("score"), Score);
						TSharedPtr<FJsonObject> Position = MakeShareable(new FJsonObject);
						Position->SetNumberField(TEXT("x"), Node->NodePosX);
						Position->SetNumberField(TEXT("y"), Node->NodePosY);
						Object->SetObjectField(TEXT("position"), Position);

						FSearchItem Item;
						Item.Score = Score;
						Item.Object = Object;
						OutItems.Add(Item);
					}
				}
			}
		}
	}

	void CollectLevelEntityResults(
		UWorld* World,
		const FString& Query,
		const FString& ClassFilter,
		const FString& FolderFilter,
		const FString& TagFilter,
		bool bIncludeHidden,
		bool bIncludeTransform,
		bool bIncludeBounds,
		int32 Limit,
		const FMcpToolContext& Context,
		TArray<FSearchItem>& OutItems,
		int32& OutActorsScanned)
	{
		OutActorsScanned = 0;
		if (!World)
		{
			return;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			++OutActorsScanned;

			if (!bIncludeHidden && Actor->IsHiddenEd())
			{
				continue;
			}

			const FString ClassName = Actor->GetClass()->GetName();
			const FString ClassPath = Actor->GetClass()->GetPathName();
			if (!ClassFilter.IsEmpty() && ScoreFields({ ClassName, ClassPath }, ClassFilter) <= 0.0)
			{
				continue;
			}
			if (!FolderFilter.IsEmpty() && ScoreText(Actor->GetFolderPath().ToString(), FolderFilter) <= 0.0)
			{
				continue;
			}

			TArray<FString> TagStrings;
			bool bTagMatched = TagFilter.IsEmpty();
			for (const FName& Tag : Actor->Tags)
			{
				const FString TagString = Tag.ToString();
				TagStrings.Add(TagString);
				if (!TagFilter.IsEmpty() && ScoreText(TagString, TagFilter) > 0.0)
				{
					bTagMatched = true;
				}
			}
			if (!bTagMatched)
			{
				continue;
			}

			TArray<FString> Fields = {
				Actor->GetActorNameOrLabel(),
				Actor->GetName(),
				ClassName,
				ClassPath,
				Actor->GetFolderPath().ToString(),
				FString::Join(TagStrings, TEXT(" "))
			};
			const double Score = ScoreFields(Fields, Query);
			if (!Query.IsEmpty() && Score <= 0.0)
			{
				continue;
			}

			FMcpEditorSessionManager::Get().RememberActor(Context.SessionId, World->GetPathName(), Actor);
			TSharedPtr<FJsonObject> Object = McpV2ToolUtils::SerializeActorSummary(Actor, Context.SessionId, bIncludeTransform, bIncludeBounds);
			if (!Object.IsValid())
			{
				continue;
			}
			Object->SetStringField(TEXT("kind"), TEXT("level_entity"));
			Object->SetStringField(TEXT("folder_path"), Actor->GetFolderPath().ToString());
			Object->SetNumberField(TEXT("score"), Score);

			TArray<TSharedPtr<FJsonValue>> TagsArray;
			for (const FString& TagString : TagStrings)
			{
				TagsArray.Add(MakeShareable(new FJsonValueString(TagString)));
			}
			Object->SetArrayField(TEXT("tags"), TagsArray);

			FSearchItem Item;
			Item.Score = Score;
			Item.Object = Object;
			OutItems.Add(Item);
		}
	}
}
