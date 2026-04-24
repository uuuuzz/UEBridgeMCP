// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Foliage/QueryFoliageSummaryTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> SerializeVector(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	TSharedPtr<FJsonObject> SerializeTransform(const FTransform& Transform)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetArrayField(TEXT("location"), SerializeVector(Transform.GetLocation()));
		Object->SetArrayField(TEXT("rotation"), SerializeVector(Transform.Rotator().Euler()));
		Object->SetArrayField(TEXT("scale"), SerializeVector(Transform.GetScale3D()));
		return Object;
	}

	FString GetMeshPath(const UFoliageType* FoliageType)
	{
		const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
		const UStaticMesh* StaticMesh = MeshType ? MeshType->GetStaticMesh() : nullptr;
		return StaticMesh ? StaticMesh->GetPathName() : FString();
	}

	bool MatchesObjectOrPackagePath(const UObject* Object, const FString& Filter)
	{
		if (!Object || Filter.IsEmpty())
		{
			return true;
		}

		if (Object->GetPathName().Equals(Filter, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const UPackage* Package = Object->GetOutermost();
		return Package && Package->GetName().Equals(Filter, ESearchCase::IgnoreCase);
	}

	bool MatchesFilter(const UFoliageType* FoliageType, const FString& FoliageTypePath, const FString& MeshPath)
	{
		if (!FoliageType)
		{
			return false;
		}

		if (!MatchesObjectOrPackagePath(FoliageType, FoliageTypePath))
		{
			return false;
		}

		const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
		const UStaticMesh* StaticMesh = MeshType ? MeshType->GetStaticMesh() : nullptr;
		if (!MatchesObjectOrPackagePath(StaticMesh, MeshPath))
		{
			return false;
		}

		return true;
	}

	TSharedPtr<FJsonObject> SerializeFoliageTypeSummary(const UFoliageType* FoliageType, const FFoliageInfo& FoliageInfo, bool bIncludeInstances, int32 MaxInstances)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!FoliageType)
		{
			return Object;
		}

		const FString MeshPath = GetMeshPath(FoliageType);
		Object->SetStringField(TEXT("foliage_type_path"), FoliageType->GetPathName());
		Object->SetStringField(TEXT("foliage_type_class"), FoliageType->GetClass()->GetName());
		if (!MeshPath.IsEmpty())
		{
			Object->SetStringField(TEXT("mesh_path"), MeshPath);
		}
		Object->SetNumberField(TEXT("instance_count"), FoliageInfo.Instances.Num());

		FBox Bounds(EForceInit::ForceInit);
		for (const FFoliageInstance& Instance : FoliageInfo.Instances)
		{
			Bounds += Instance.GetInstanceWorldTransform().GetLocation();
		}
		const bool bHasBounds = Bounds.IsValid != 0;
		Object->SetBoolField(TEXT("has_bounds"), bHasBounds);
		if (bHasBounds)
		{
			Object->SetObjectField(TEXT("bounds"), McpV2ToolUtils::SerializeBounds(Bounds.GetCenter(), Bounds.GetExtent(), Bounds.GetExtent().Size()));
		}

		if (bIncludeInstances)
		{
			TArray<TSharedPtr<FJsonValue>> InstanceArray;
			const int32 InstanceLimit = MaxInstances <= 0 ? FoliageInfo.Instances.Num() : FMath::Min(MaxInstances, FoliageInfo.Instances.Num());
			for (int32 Index = 0; Index < InstanceLimit; ++Index)
			{
				TSharedPtr<FJsonObject> InstanceObject = MakeShareable(new FJsonObject);
				InstanceObject->SetNumberField(TEXT("index"), Index);
				InstanceObject->SetObjectField(TEXT("transform"), SerializeTransform(FoliageInfo.Instances[Index].GetInstanceWorldTransform()));
				InstanceArray.Add(MakeShareable(new FJsonValueObject(InstanceObject)));
			}
			Object->SetArrayField(TEXT("instances"), InstanceArray);
			Object->SetBoolField(TEXT("instances_truncated"), InstanceLimit < FoliageInfo.Instances.Num());
		}

		return Object;
	}
}

FString UQueryFoliageSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize foliage instances in an editor or PIE world, grouped by foliage type and static mesh.");
}

TMap<FString, FMcpSchemaProperty> UQueryFoliageSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to inspect"), { TEXT("editor"), TEXT("pie") }));
	Schema.Add(TEXT("foliage_type_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional foliage type asset path filter")));
	Schema.Add(TEXT("mesh_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional static mesh asset path filter")));
	Schema.Add(TEXT("include_instances"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include sample instance transforms. Default: false")));
	Schema.Add(TEXT("max_instances"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum instances to include per foliage type when include_instances=true. Default: 32")));
	return Schema;
}

FMcpToolResult UQueryFoliageSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString FoliageTypePath = GetStringArgOrDefault(Arguments, TEXT("foliage_type_path"));
	const FString MeshPath = GetStringArgOrDefault(Arguments, TEXT("mesh_path"));
	const bool bIncludeInstances = GetBoolArgOrDefault(Arguments, TEXT("include_instances"), false);
	const int32 MaxInstances = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_instances"), 32));

	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the requested world"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("world_path"), World->GetPathName());
	Result->SetStringField(TEXT("world_type"), RequestedWorldType);
	Result->SetBoolField(TEXT("has_instanced_foliage_actor"), false);

	AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::Get(World, false);
	if (!FoliageActor)
	{
		Result->SetNumberField(TEXT("foliage_type_count"), 0);
		Result->SetNumberField(TEXT("instance_count"), 0);
		Result->SetArrayField(TEXT("foliage_types"), TArray<TSharedPtr<FJsonValue>>());
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Foliage summary ready"));
	}

	Result->SetBoolField(TEXT("has_instanced_foliage_actor"), true);
	Result->SetStringField(TEXT("instanced_foliage_actor"), FoliageActor->GetPathName());

	int32 TotalInstances = 0;
	TArray<TSharedPtr<FJsonValue>> FoliageTypesArray;
	for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : FoliageActor->GetFoliageInfos())
	{
		const UFoliageType* FoliageType = Pair.Key;
		const FFoliageInfo& FoliageInfo = Pair.Value.Get();
		if (!MatchesFilter(FoliageType, FoliageTypePath, MeshPath))
		{
			continue;
		}

		TotalInstances += FoliageInfo.Instances.Num();
		FoliageTypesArray.Add(MakeShareable(new FJsonValueObject(SerializeFoliageTypeSummary(FoliageType, FoliageInfo, bIncludeInstances, MaxInstances))));
	}

	Result->SetNumberField(TEXT("foliage_type_count"), FoliageTypesArray.Num());
	Result->SetNumberField(TEXT("instance_count"), TotalInstances);
	Result->SetArrayField(TEXT("foliage_types"), FoliageTypesArray);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Foliage summary ready"));
}
