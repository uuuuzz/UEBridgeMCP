// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/QueryNavigationStateTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "EngineUtils.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

FString UQueryNavigationStateTool::GetToolDescription() const
{
	return TEXT("Query navigation system state for the target world, including registered nav data and bounds volumes.");
}

TMap<FString, FMcpSchemaProperty> UQueryNavigationStateTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("include_bounds"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include bounds for nav data and bounds volumes")));
	return Schema;
}

FMcpToolResult UQueryNavigationStateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bIncludeBounds = GetBoolArgOrDefault(Arguments, TEXT("include_bounds"), true);

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SUBSYSTEM_UNAVAILABLE"), TEXT("NavigationSystemV1 is not available for the target world"));
	}

	TArray<TSharedPtr<FJsonValue>> NavDataArray;
	for (TActorIterator<ANavigationData> It(World); It; ++It)
	{
		ANavigationData* NavData = *It;
		if (!NavData)
		{
			continue;
		}

		TSharedPtr<FJsonObject> NavDataObject = McpV2ToolUtils::SerializeActorSummary(NavData, Context.SessionId, true, bIncludeBounds);
		NavDataObject->SetStringField(TEXT("class"), NavData->GetClass()->GetPathName());
		NavDataObject->SetBoolField(TEXT("supports_runtime_generation"), NavData->SupportsRuntimeGeneration());

		if (const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData))
		{
			TSharedPtr<FJsonObject> RecastObject = MakeShareable(new FJsonObject);
			RecastObject->SetNumberField(TEXT("tile_size_uu"), RecastNavMesh->GetTileSizeUU());
			RecastObject->SetNumberField(TEXT("cell_size"), RecastNavMesh->GetCellSize(ENavigationDataResolution::Default));
			RecastObject->SetNumberField(TEXT("cell_height"), RecastNavMesh->GetCellHeight(ENavigationDataResolution::Default));
			RecastObject->SetNumberField(TEXT("agent_radius"), RecastNavMesh->AgentRadius);
			RecastObject->SetNumberField(TEXT("agent_height"), RecastNavMesh->AgentHeight);
			NavDataObject->SetObjectField(TEXT("recast"), RecastObject);
		}

		NavDataArray.Add(MakeShareable(new FJsonValueObject(NavDataObject)));
	}

	TArray<TSharedPtr<FJsonValue>> BoundsVolumeArray;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		if (ANavMeshBoundsVolume* BoundsVolume = *It)
		{
			BoundsVolumeArray.Add(MakeShareable(new FJsonValueObject(
				McpV2ToolUtils::SerializeActorSummary(BoundsVolume, Context.SessionId, true, bIncludeBounds))));
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("world_name"), World->GetName());
	Response->SetStringField(TEXT("world_path"), World->GetPathName());
	Response->SetArrayField(TEXT("nav_data"), NavDataArray);
	Response->SetArrayField(TEXT("nav_mesh_bounds_volumes"), BoundsVolumeArray);
	Response->SetBoolField(TEXT("has_main_nav_data"), NavigationSystem->GetDefaultNavDataInstance() != nullptr);
	return FMcpToolResult::StructuredJson(Response);
}
