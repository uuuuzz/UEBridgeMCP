// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Performance/PerformanceToolUtils.h"

#include "Utils/McpAssetModifier.h"

#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "UObject/UObjectArray.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Value)
	{
		return {
			MakeShareable(new FJsonValueNumber(Value.X)),
			MakeShareable(new FJsonValueNumber(Value.Y)),
			MakeShareable(new FJsonValueNumber(Value.Z))
		};
	}
}

namespace PerformanceToolUtils
{
	UWorld* ResolveWorld(const FString& RequestedWorldType)
	{
		return FMcpAssetModifier::ResolveWorld(RequestedWorldType.IsEmpty() ? TEXT("editor") : RequestedWorldType);
	}

	TSharedPtr<FJsonObject> BuildPerformanceReport(UWorld* World, const FString& RequestedWorldType)
	{
		TSharedPtr<FJsonObject> Report = MakeShareable(new FJsonObject);
		Report->SetStringField(TEXT("world_mode"), RequestedWorldType.IsEmpty() ? TEXT("editor") : RequestedWorldType);

		const double DeltaTime = FApp::GetDeltaTime();
		const double FrameTimeMs = DeltaTime > 0.0 ? DeltaTime * 1000.0 : 0.0;
		const double Fps = DeltaTime > 0.0 ? 1.0 / DeltaTime : 0.0;
		Report->SetNumberField(TEXT("fps"), Fps);
		Report->SetNumberField(TEXT("frame_time_ms"), FrameTimeMs);

		if (World)
		{
			Report->SetStringField(TEXT("world_name"), World->GetName());
			Report->SetStringField(TEXT("world_path"), World->GetPathName());
			Report->SetBoolField(TEXT("world_partition_enabled"), World->GetWorldPartition() != nullptr);

			int32 ActorCount = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It)
				{
					++ActorCount;
				}
			}

			int32 VisibleStreamingLevels = 0;
			for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
			{
				if (StreamingLevel && StreamingLevel->GetShouldBeVisibleFlag())
				{
					++VisibleStreamingLevels;
				}
			}

			Report->SetNumberField(TEXT("actor_count"), ActorCount);
			Report->SetNumberField(TEXT("streaming_level_count"), World->GetStreamingLevels().Num());
			Report->SetNumberField(TEXT("visible_streaming_level_count"), VisibleStreamingLevels);

			if (World->PersistentLevel)
			{
				FBox Bounds(ForceInit);
				for (AActor* Actor : World->PersistentLevel->Actors)
				{
					if (Actor)
					{
						Bounds += Actor->GetComponentsBoundingBox(true);
					}
				}

				if (Bounds.IsValid)
				{
					TSharedPtr<FJsonObject> BoundsObject = MakeShareable(new FJsonObject);
					BoundsObject->SetArrayField(TEXT("center"), VectorToArray(Bounds.GetCenter()));
					BoundsObject->SetArrayField(TEXT("extent"), VectorToArray(Bounds.GetExtent()));
					Report->SetObjectField(TEXT("world_bounds"), BoundsObject);
				}
			}
		}

		Report->SetNumberField(TEXT("uobject_count"), GUObjectArray.GetObjectArrayNum());

		const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
		TSharedPtr<FJsonObject> MemoryObject = MakeShareable(new FJsonObject);
		MemoryObject->SetNumberField(TEXT("physical_used_mb"), static_cast<double>(MemoryStats.UsedPhysical) / (1024.0 * 1024.0));
		MemoryObject->SetNumberField(TEXT("physical_available_mb"), static_cast<double>(MemoryStats.AvailablePhysical) / (1024.0 * 1024.0));
		MemoryObject->SetNumberField(TEXT("virtual_used_mb"), static_cast<double>(MemoryStats.UsedVirtual) / (1024.0 * 1024.0));
		MemoryObject->SetNumberField(TEXT("virtual_available_mb"), static_cast<double>(MemoryStats.AvailableVirtual) / (1024.0 * 1024.0));
		Report->SetObjectField(TEXT("memory"), MemoryObject);

		return Report;
	}
}
