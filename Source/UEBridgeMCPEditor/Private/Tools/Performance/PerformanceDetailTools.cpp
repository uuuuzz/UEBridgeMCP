// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Performance/PerformanceDetailTools.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "UnrealClient.h"

namespace
{
	UWorld* ResolveWorld()
	{
		return FMcpAssetModifier::ResolveWorld(TEXT("editor"));
	}

	FEditorViewportClient* GetViewportClient()
	{
		if (!GEditor || !GEditor->GetActiveViewport())
		{
			return nullptr;
		}
		return static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
	}

	bool IsNaniteEnabled(const UStaticMesh* StaticMesh)
	{
		if (!StaticMesh)
		{
			return false;
		}
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		return StaticMesh->NaniteSettings.bEnabled;
#else
		return StaticMesh->GetNaniteSettings().bEnabled;
#endif
	}

	int64 EstimateTriangles(UStaticMesh* Mesh, int32 LodIndex = 0)
	{
		if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() <= LodIndex)
		{
			return 0;
		}
		return Mesh->GetRenderData()->LODResources[LodIndex].GetNumTriangles();
	}

	int32 EstimateDrawCalls(UStaticMesh* Mesh, int32 LodIndex = 0)
	{
		if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() <= LodIndex)
		{
			return 0;
		}
		return Mesh->GetRenderData()->LODResources[LodIndex].Sections.Num();
	}

	TSharedPtr<FJsonObject> MakeActorCost(AActor* Actor, const FString& SessionId, const FVector& ViewLocation)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Actor ? Actor->GetName() : TEXT(""));
		Object->SetStringField(TEXT("label"), Actor ? Actor->GetActorNameOrLabel() : TEXT(""));
		if (!Actor)
		{
			return Object;
		}

		int64 Triangles = 0;
		int32 DrawCalls = 0;
		int32 StaticMeshComponents = 0;
		int32 PrimitiveComponents = 0;
		int32 ShadowCasters = 0;
		int32 NaniteComponents = 0;

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				++PrimitiveComponents;
				if (Primitive->CastShadow)
				{
					++ShadowCasters;
				}
			}

			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				++StaticMeshComponents;
				UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh();
				Triangles += EstimateTriangles(Mesh);
				DrawCalls += EstimateDrawCalls(Mesh);
				if (IsNaniteEnabled(Mesh))
				{
					++NaniteComponents;
				}
			}
		}

		Object->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, true, true));
		Object->SetNumberField(TEXT("estimated_triangles"), static_cast<double>(Triangles));
		Object->SetNumberField(TEXT("estimated_draw_calls"), DrawCalls);
		Object->SetNumberField(TEXT("static_mesh_component_count"), StaticMeshComponents);
		Object->SetNumberField(TEXT("primitive_component_count"), PrimitiveComponents);
		Object->SetNumberField(TEXT("shadow_caster_count"), ShadowCasters);
		Object->SetNumberField(TEXT("nanite_component_count"), NaniteComponents);
		Object->SetNumberField(TEXT("distance_to_view"), FVector::Dist(ViewLocation, Actor->GetActorLocation()));
		Object->SetNumberField(TEXT("score"), static_cast<double>(DrawCalls * 1000 + ShadowCasters * 250 + StaticMeshComponents * 100) + (static_cast<double>(Triangles) / 1000.0));
		return Object;
	}

	FString CategorizeAssetClass(const FString& ClassName)
	{
		if (ClassName.Contains(TEXT("Texture"))) return TEXT("textures");
		if (ClassName.Contains(TEXT("StaticMesh"))) return TEXT("static_meshes");
		if (ClassName.Contains(TEXT("Blueprint"))) return TEXT("blueprints");
		if (ClassName.Contains(TEXT("Material"))) return TEXT("materials");
		if (ClassName.Contains(TEXT("Anim")) || ClassName.Contains(TEXT("Skeleton"))) return TEXT("animation");
		if (ClassName.Contains(TEXT("Sound")) || ClassName.Contains(TEXT("MetaSound"))) return TEXT("audio");
		if (ClassName.Contains(TEXT("Niagara"))) return TEXT("niagara");
		return TEXT("other");
	}
}

FString UQueryRenderStatsTool::GetToolDescription() const
{
	return TEXT("Return editor-world render-oriented estimates: actors, static mesh triangles, draw calls, lights, shadows, and warnings.");
}

TMap<FString, FMcpSchemaProperty> UQueryRenderStatsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("include_top_classes"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include top actor class distribution. Default: true.")));
	return Schema;
}

FMcpToolResult UQueryRenderStatsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;
	UWorld* World = ResolveWorld();
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No editor world is currently available"));
	}

	const bool bIncludeTopClasses = GetBoolArgOrDefault(Arguments, TEXT("include_top_classes"), true);

	int32 ActorCount = 0;
	int32 StaticMeshComponentCount = 0;
	int64 TriangleCount = 0;
	int32 DrawCallCount = 0;
	int32 ShadowCasterCount = 0;
	int32 LightCount = 0;
	TMap<FString, int32> ClassCounts;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		++ActorCount;
		ClassCounts.FindOrAdd(Actor->GetClass()->GetName())++;

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		Actor->GetComponents(StaticMeshComponents);
		for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
		{
			if (!StaticMeshComponent || !StaticMeshComponent->GetStaticMesh())
			{
				continue;
			}
			++StaticMeshComponentCount;
			TriangleCount += EstimateTriangles(StaticMeshComponent->GetStaticMesh());
			DrawCallCount += EstimateDrawCalls(StaticMeshComponent->GetStaticMesh());
			if (StaticMeshComponent->CastShadow)
			{
				++ShadowCasterCount;
			}
		}

		TArray<ULightComponent*> LightComponents;
		Actor->GetComponents(LightComponents);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (LightComponent)
			{
				++LightCount;
				if (LightComponent->CastShadows)
				{
					++ShadowCasterCount;
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (DrawCallCount > 5000)
	{
		Warnings.Add(MakeShareable(new FJsonValueString(TEXT("High estimated draw call count"))));
	}
	if (TriangleCount > 10000000)
	{
		Warnings.Add(MakeShareable(new FJsonValueString(TEXT("High estimated triangle count"))));
	}
	if (ShadowCasterCount > 100)
	{
		Warnings.Add(MakeShareable(new FJsonValueString(TEXT("High shadow caster count"))));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("world"), World->GetPathName());
	Response->SetNumberField(TEXT("actor_count"), ActorCount);
	Response->SetNumberField(TEXT("static_mesh_component_count"), StaticMeshComponentCount);
	Response->SetNumberField(TEXT("estimated_triangles"), static_cast<double>(TriangleCount));
	Response->SetNumberField(TEXT("estimated_draw_calls"), DrawCallCount);
	Response->SetNumberField(TEXT("light_component_count"), LightCount);
	Response->SetNumberField(TEXT("shadow_caster_count"), ShadowCasterCount);
	Response->SetArrayField(TEXT("warnings"), Warnings);

	if (bIncludeTopClasses)
	{
		ClassCounts.ValueSort([](int32 A, int32 B) { return A > B; });
		TArray<TSharedPtr<FJsonValue>> Classes;
		int32 Added = 0;
		for (const TPair<FString, int32>& Pair : ClassCounts)
		{
			if (Added++ >= 15)
			{
				break;
			}
			TSharedPtr<FJsonObject> ClassObject = MakeShareable(new FJsonObject);
			ClassObject->SetStringField(TEXT("class"), Pair.Key);
			ClassObject->SetNumberField(TEXT("count"), Pair.Value);
			Classes.Add(MakeShareable(new FJsonValueObject(ClassObject)));
		}
		Response->SetArrayField(TEXT("top_classes"), Classes);
	}

	return FMcpToolResult::StructuredJson(Response);
}

FString UQueryMemoryReportTool::GetToolDescription() const
{
	return TEXT("Return system memory plus asset disk-size totals grouped by asset category.");
}

TMap<FString, FMcpSchemaProperty> UQueryMemoryReportTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Content path to scan. Default: /Game")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Top assets per category. Default: 10, max: 50.")));
	return Schema;
}

FMcpToolResult UQueryMemoryReportTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Path = GetStringArgOrDefault(Arguments, TEXT("path"), TEXT("/Game"));
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 10), 1, 50);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*Path), Assets, true);

	struct FCategoryInfo
	{
		int64 TotalBytes = 0;
		TArray<TPair<int64, FString>> Assets;
	};
	TMap<FString, FCategoryInfo> Categories;

	for (const FAssetData& Asset : Assets)
	{
		const FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		const FString Category = CategorizeAssetClass(ClassName);
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(Asset.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
		const int64 FileSize = IFileManager::Get().FileSize(*PackageFileName);
		if (FileSize <= 0)
		{
			continue;
		}

		FCategoryInfo& CategoryInfo = Categories.FindOrAdd(Category);
		CategoryInfo.TotalBytes += FileSize;
		CategoryInfo.Assets.Add(TPair<int64, FString>(FileSize, Asset.GetObjectPathString()));
	}

	TArray<TSharedPtr<FJsonValue>> CategoryArray;
	int64 TotalBytes = 0;
	for (TPair<FString, FCategoryInfo>& Pair : Categories)
	{
		Pair.Value.Assets.Sort([](const TPair<int64, FString>& A, const TPair<int64, FString>& B) { return A.Key > B.Key; });
		TotalBytes += Pair.Value.TotalBytes;

		TArray<TSharedPtr<FJsonValue>> TopAssets;
		for (int32 Index = 0; Index < FMath::Min(Limit, Pair.Value.Assets.Num()); ++Index)
		{
			TSharedPtr<FJsonObject> AssetObject = MakeShareable(new FJsonObject);
			AssetObject->SetStringField(TEXT("object_path"), Pair.Value.Assets[Index].Value);
			AssetObject->SetNumberField(TEXT("size_bytes"), static_cast<double>(Pair.Value.Assets[Index].Key));
			TopAssets.Add(MakeShareable(new FJsonValueObject(AssetObject)));
		}

		TSharedPtr<FJsonObject> CategoryObject = MakeShareable(new FJsonObject);
		CategoryObject->SetStringField(TEXT("category"), Pair.Key);
		CategoryObject->SetNumberField(TEXT("asset_count"), Pair.Value.Assets.Num());
		CategoryObject->SetNumberField(TEXT("total_bytes"), static_cast<double>(Pair.Value.TotalBytes));
		CategoryObject->SetArrayField(TEXT("top_assets"), TopAssets);
		CategoryArray.Add(MakeShareable(new FJsonValueObject(CategoryObject)));
	}

	FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	TSharedPtr<FJsonObject> SystemMemory = MakeShareable(new FJsonObject);
	SystemMemory->SetNumberField(TEXT("used_physical_mb"), static_cast<double>(MemoryStats.UsedPhysical) / 1024.0 / 1024.0);
	SystemMemory->SetNumberField(TEXT("available_physical_mb"), static_cast<double>(MemoryStats.AvailablePhysical) / 1024.0 / 1024.0);
	SystemMemory->SetNumberField(TEXT("used_virtual_mb"), static_cast<double>(MemoryStats.UsedVirtual) / 1024.0 / 1024.0);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("path"), Path);
	Response->SetNumberField(TEXT("asset_count"), Assets.Num());
	Response->SetNumberField(TEXT("total_bytes"), static_cast<double>(TotalBytes));
	Response->SetObjectField(TEXT("system_memory"), SystemMemory);
	Response->SetArrayField(TEXT("categories"), CategoryArray);
	return FMcpToolResult::StructuredJson(Response);
}

FString UProfileVisibleActorsTool::GetToolDescription() const
{
	return TEXT("Rank editor-world actors by estimated render cost using static mesh triangles, draw calls, shadows, and distance to the active viewport.");
}

TMap<FString, FMcpSchemaProperty> UProfileVisibleActorsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum actor cost rows. Default: 20, max: 200.")));
	Schema.Add(TEXT("include_zero_cost"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actors with no primitive/static mesh cost. Default: false.")));
	return Schema;
}

FMcpToolResult UProfileVisibleActorsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	UWorld* World = ResolveWorld();
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No editor world is currently available"));
	}

	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 20), 1, 200);
	const bool bIncludeZeroCost = GetBoolArgOrDefault(Arguments, TEXT("include_zero_cost"), false);
	const FVector ViewLocation = GetViewportClient() ? GetViewportClient()->GetViewLocation() : FVector::ZeroVector;

	TArray<TSharedPtr<FJsonObject>> ActorCosts;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		TSharedPtr<FJsonObject> CostObject = MakeActorCost(Actor, Context.SessionId, ViewLocation);
		double Score = 0.0;
		CostObject->TryGetNumberField(TEXT("score"), Score);
		if (Score > 0.0 || bIncludeZeroCost)
		{
			ActorCosts.Add(CostObject);
		}
	}

	ActorCosts.Sort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
	{
		double Left = 0.0;
		double Right = 0.0;
		A->TryGetNumberField(TEXT("score"), Left);
		B->TryGetNumberField(TEXT("score"), Right);
		return Left > Right;
	});

	TArray<TSharedPtr<FJsonValue>> Rows;
	for (int32 Index = 0; Index < FMath::Min(Limit, ActorCosts.Num()); ++Index)
	{
		Rows.Add(MakeShareable(new FJsonValueObject(ActorCosts[Index])));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("world"), World->GetPathName());
	Response->SetNumberField(TEXT("profiled_actor_count"), ActorCosts.Num());
	Response->SetArrayField(TEXT("actors"), Rows);
	return FMcpToolResult::StructuredJson(Response);
}
