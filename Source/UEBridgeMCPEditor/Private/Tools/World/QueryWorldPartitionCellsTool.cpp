// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/World/QueryWorldPartitionCellsTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpOptionalCapabilityUtils.h"
#include "Utils/McpV2ToolUtils.h"

#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"

namespace
{
	FString CellStateToString(EWorldPartitionRuntimeCellState State)
	{
		switch (State)
		{
		case EWorldPartitionRuntimeCellState::Unloaded:
			return TEXT("unloaded");
		case EWorldPartitionRuntimeCellState::Loaded:
			return TEXT("loaded");
		case EWorldPartitionRuntimeCellState::Activated:
			return TEXT("activated");
		default:
			return TEXT("unknown");
		}
	}

	FString StreamingStatusToString(EStreamingStatus Status)
	{
		switch (Status)
		{
		case LEVEL_Visible:
			return TEXT("visible");
		case LEVEL_MakingVisible:
			return TEXT("making_visible");
		case LEVEL_Loaded:
			return TEXT("loaded");
		case LEVEL_MakingInvisible:
			return TEXT("making_invisible");
		case LEVEL_UnloadedButStillAround:
			return TEXT("unloaded_but_still_around");
		case LEVEL_Unloaded:
			return TEXT("unloaded");
		case LEVEL_Preloading:
			return TEXT("preloading");
		case LEVEL_FailedToLoad:
			return TEXT("failed_to_load");
		default:
			return TEXT("unknown");
		}
	}

	TArray<TSharedPtr<FJsonValue>> ToStringArray(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FName& Name : Names)
		{
			Result.Add(MakeShareable(new FJsonValueString(Name.ToString())));
		}
		return Result;
	}
}

FString UQueryWorldPartitionCellsTool::GetToolDescription() const
{
	return TEXT("Query visible world partition runtime cells, including bounds, streaming status, actor count, and data layer names.");
}

TMap<FString, FMcpSchemaProperty> UQueryWorldPartitionCellsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to inspect"), { TEXT("editor"), TEXT("pie") }));
	Schema.Add(TEXT("include_content_bounds"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include content bounds in each cell summary")));
	return Schema;
}

FMcpToolResult UQueryWorldPartitionCellsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bIncludeContentBounds = GetBoolArgOrDefault(Arguments, TEXT("include_content_bounds"), true);

	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the requested world"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("world_path"), World->GetPathName());
	Result->SetBoolField(TEXT("supported"), FMcpOptionalCapabilityUtils::IsWorldPartitionAvailable());
	Result->SetBoolField(TEXT("world_partition_enabled"), World->GetWorldPartition() != nullptr);

	if (!World->GetWorldPartition())
	{
		Result->SetArrayField(TEXT("cells"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetNumberField(TEXT("cell_count"), 0);
		return FMcpToolResult::StructuredSuccess(Result, TEXT("World partition is not enabled for the requested world"));
	}

	TArray<TSharedPtr<FJsonValue>> CellsArray;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		const UWorldPartitionRuntimeCell* RuntimeCell = StreamingLevel ? Cast<UWorldPartitionRuntimeCell>(StreamingLevel->GetWorldPartitionCell()) : nullptr;
		if (!RuntimeCell)
		{
			continue;
		}

		TSharedPtr<FJsonObject> CellObject = MakeShareable(new FJsonObject);
		CellObject->SetStringField(TEXT("debug_name"), RuntimeCell->GetDebugName());
		CellObject->SetStringField(TEXT("guid"), RuntimeCell->GetGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		CellObject->SetStringField(TEXT("state"), CellStateToString(RuntimeCell->GetCurrentState()));
		CellObject->SetStringField(TEXT("streaming_status"), StreamingStatusToString(RuntimeCell->GetStreamingStatus()));
		CellObject->SetBoolField(TEXT("loaded"), RuntimeCell->GetCurrentState() != EWorldPartitionRuntimeCellState::Unloaded);
		CellObject->SetBoolField(TEXT("activated"), RuntimeCell->GetCurrentState() == EWorldPartitionRuntimeCellState::Activated);
		CellObject->SetBoolField(TEXT("always_loaded"), RuntimeCell->IsAlwaysLoaded());
		CellObject->SetBoolField(TEXT("spatially_loaded"), RuntimeCell->IsSpatiallyLoaded());
		CellObject->SetStringField(TEXT("level_package_name"), RuntimeCell->GetLevelPackageName().ToString());
		CellObject->SetArrayField(TEXT("data_layers"), ToStringArray(RuntimeCell->GetDataLayers()));
		CellObject->SetObjectField(TEXT("cell_bounds"), McpV2ToolUtils::SerializeBounds(RuntimeCell->GetCellBounds().GetCenter(), RuntimeCell->GetCellBounds().GetExtent(), RuntimeCell->GetCellBounds().GetExtent().Size()));
		CellObject->SetObjectField(TEXT("streaming_bounds"), McpV2ToolUtils::SerializeBounds(RuntimeCell->GetStreamingBounds().GetCenter(), RuntimeCell->GetStreamingBounds().GetExtent(), RuntimeCell->GetStreamingBounds().GetExtent().Size()));
		CellObject->SetNumberField(TEXT("actor_count"), RuntimeCell->GetActorCount());

		if (bIncludeContentBounds && RuntimeCell->GetContentBounds().IsValid)
		{
			const FBox ContentBounds = RuntimeCell->GetContentBounds();
			CellObject->SetObjectField(TEXT("content_bounds"), McpV2ToolUtils::SerializeBounds(ContentBounds.GetCenter(), ContentBounds.GetExtent(), ContentBounds.GetExtent().Size()));
		}

		CellsArray.Add(MakeShareable(new FJsonValueObject(CellObject)));
	}

	Result->SetArrayField(TEXT("cells"), CellsArray);
	Result->SetNumberField(TEXT("cell_count"), CellsArray.Num());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("World partition cell summary ready"));
}
