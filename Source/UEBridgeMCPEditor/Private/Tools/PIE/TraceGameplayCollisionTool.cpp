// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/TraceGameplayCollisionTool.h"

#include "Tools/Gameplay/RuntimeGameplayToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "CollisionShape.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	bool IsFieldPresent(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		return Object.IsValid() && Object->HasField(FieldName);
	}
}

FString UTraceGameplayCollisionTool::GetToolDescription() const
{
	return TEXT("Run a read-only gameplay collision trace in the editor or PIE world. Supports line, sphere, capsule, and box traces by collision channel.");
}

TMap<FString, FMcpSchemaProperty> UTraceGameplayCollisionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("start"), FMcpSchemaProperty::Make(TEXT("array"), TEXT("Trace start as [x,y,z] or {x,y,z}"), true));
	Schema.Add(TEXT("end"), FMcpSchemaProperty::Make(TEXT("array"), TEXT("Trace end as [x,y,z] or {x,y,z}"), true));
	Schema.Add(TEXT("shape"), FMcpSchemaProperty::MakeEnum(TEXT("Trace shape"), { TEXT("line"), TEXT("sphere"), TEXT("capsule"), TEXT("box") }));
	Schema.Add(TEXT("channel"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Collision channel, e.g. Visibility, Camera, WorldStatic, WorldDynamic, Pawn")));
	Schema.Add(TEXT("radius"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Sphere radius or capsule radius")));
	Schema.Add(TEXT("half_height"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Capsule half height")));
	Schema.Add(TEXT("box_extent"), FMcpSchemaProperty::Make(TEXT("array"), TEXT("Box half extent as [x,y,z] or {x,y,z}")));
	Schema.Add(TEXT("trace_complex"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Use complex collision")));
	Schema.Add(TEXT("return_all"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Return all hits instead of the first blocking hit")));
	Schema.Add(TEXT("max_hits"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum hits returned when return_all=true")));
	Schema.Add(TEXT("ignore_actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor label/name to ignore")));
	Schema.Add(TEXT("ignore_actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional actor handle to ignore")));
	return Schema;
}

FMcpToolResult UTraceGameplayCollisionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available for collision trace"));
	}

	FString VectorError;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	if (!RuntimeGameplayToolUtils::TryReadVectorField(Arguments, TEXT("start"), Start, VectorError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), VectorError);
	}
	if (!RuntimeGameplayToolUtils::TryReadVectorField(Arguments, TEXT("end"), End, VectorError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), VectorError);
	}

	ECollisionChannel Channel = ECC_Visibility;
	const FString ChannelName = GetStringArgOrDefault(Arguments, TEXT("channel"), TEXT("Visibility"));
	FString ChannelError;
	if (!PhysicsToolUtils::TryParseCollisionChannel(ChannelName, Channel, ChannelError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_COLLISION_CHANNEL"), ChannelError);
	}

	AActor* IgnoreActor = nullptr;
	if (IsFieldPresent(Arguments, TEXT("ignore_actor_name")) || IsFieldPresent(Arguments, TEXT("ignore_actor_handle")))
	{
		UWorld* IgnoreWorld = nullptr;
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ErrorDetails;
		IgnoreActor = LevelActorToolUtils::ResolveActorReference(
			Arguments,
			WorldType,
			TEXT("ignore_actor_name"),
			TEXT("ignore_actor_handle"),
			Context,
			IgnoreWorld,
			ErrorCode,
			ErrorMessage,
			ErrorDetails,
			true);
		if (!IgnoreActor)
		{
			return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
		}
		if (IgnoreWorld && IgnoreWorld != World)
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("trace_world"), World->GetPathName());
			Details->SetStringField(TEXT("ignore_actor_world"), IgnoreWorld->GetPathName());
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_MISMATCH"), TEXT("ignore actor resolved to a different world"), Details);
		}
	}

	const FString ShapeName = RuntimeGameplayToolUtils::NormalizeSectionName(GetStringArgOrDefault(Arguments, TEXT("shape"), TEXT("line")));
	const bool bTraceComplex = GetBoolArgOrDefault(Arguments, TEXT("trace_complex"), false);
	const bool bReturnAll = GetBoolArgOrDefault(Arguments, TEXT("return_all"), false);
	const int32 MaxHits = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("max_hits"), 32));

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UEBridgeMCPTraceGameplayCollision), bTraceComplex);
	if (IgnoreActor)
	{
		QueryParams.AddIgnoredActor(IgnoreActor);
	}

	TArray<FHitResult> Hits;
	bool bAnyHit = false;
	bool bBlockingHit = false;
	if (ShapeName == TEXT("line"))
	{
		if (bReturnAll)
		{
			bAnyHit = World->LineTraceMultiByChannel(Hits, Start, End, Channel, QueryParams);
		}
		else
		{
			FHitResult Hit;
			bAnyHit = World->LineTraceSingleByChannel(Hit, Start, End, Channel, QueryParams);
			if (bAnyHit)
			{
				Hits.Add(Hit);
			}
		}
	}
	else
	{
		FCollisionShape CollisionShape;
		if (ShapeName == TEXT("sphere"))
		{
			const float Radius = FMath::Max(0.1f, GetFloatArgOrDefault(Arguments, TEXT("radius"), 25.0f));
			CollisionShape = FCollisionShape::MakeSphere(Radius);
		}
		else if (ShapeName == TEXT("capsule"))
		{
			const float Radius = FMath::Max(0.1f, GetFloatArgOrDefault(Arguments, TEXT("radius"), 25.0f));
			const float HalfHeight = FMath::Max(Radius, GetFloatArgOrDefault(Arguments, TEXT("half_height"), 50.0f));
			CollisionShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
		}
		else if (ShapeName == TEXT("box"))
		{
			FVector BoxExtent(25.0, 25.0, 25.0);
			if (Arguments->HasField(TEXT("box_extent")) && !RuntimeGameplayToolUtils::TryReadVectorField(Arguments, TEXT("box_extent"), BoxExtent, VectorError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), VectorError);
			}
			CollisionShape = FCollisionShape::MakeBox(BoxExtent);
		}
		else
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'shape' must be one of line, sphere, capsule, or box"));
		}

		if (bReturnAll)
		{
			bAnyHit = World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, Channel, CollisionShape, QueryParams);
		}
		else
		{
			FHitResult Hit;
			bAnyHit = World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Channel, CollisionShape, QueryParams);
			if (bAnyHit)
			{
				Hits.Add(Hit);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> HitArray;
	for (int32 Index = 0; Index < Hits.Num() && HitArray.Num() < MaxHits; ++Index)
	{
		const FHitResult& Hit = Hits[Index];
		bBlockingHit = bBlockingHit || Hit.bBlockingHit;
		TSharedPtr<FJsonObject> HitObject = RuntimeGameplayToolUtils::SerializeHitResult(Hit, Context.SessionId);
		HitObject->SetNumberField(TEXT("index"), Index);
		HitArray.Add(MakeShareable(new FJsonValueObject(HitObject)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("world"), RuntimeGameplayToolUtils::SerializeWorld(World));
	Response->SetStringField(TEXT("shape"), ShapeName);
	Response->SetStringField(TEXT("channel"), PhysicsToolUtils::CollisionChannelToString(Channel));
	Response->SetObjectField(TEXT("start"), RuntimeGameplayToolUtils::VectorToJson(Start));
	Response->SetObjectField(TEXT("end"), RuntimeGameplayToolUtils::VectorToJson(End));
	Response->SetBoolField(TEXT("hit"), bAnyHit);
	Response->SetBoolField(TEXT("blocking_hit"), bBlockingHit);
	Response->SetNumberField(TEXT("hit_count"), HitArray.Num());
	Response->SetArrayField(TEXT("hits"), HitArray);
	Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
	return FMcpToolResult::StructuredJson(Response);
}
