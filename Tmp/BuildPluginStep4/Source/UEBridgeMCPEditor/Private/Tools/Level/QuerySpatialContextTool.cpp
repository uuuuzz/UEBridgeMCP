// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QuerySpatialContextTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpV2ToolUtils.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}
}

FString UQuerySpatialContextTool::GetToolDescription() const
{
	return TEXT("Return spatial context for a set of actors, including bounds, aggregate bounds, pairwise distances, and optional ground hits.");
}

TMap<FString, FMcpSchemaProperty> UQuerySpatialContextTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));

	TSharedPtr<FMcpSchemaProperty> ActorReferenceSchema = MakeShared<FMcpSchemaProperty>();
	ActorReferenceSchema->Type = TEXT("object");
	ActorReferenceSchema->Description = TEXT("Actor reference using either actor_name or actor_handle");
	ActorReferenceSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Actor name or label"))));
	ActorReferenceSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("object"),
		TEXT("Actor handle returned by query-level-summary or query-actor-selection"))));

	FMcpSchemaProperty ActorsSchema;
	ActorsSchema.Type = TEXT("array");
	ActorsSchema.Description = TEXT("Actor references");
	ActorsSchema.bRequired = true;
	ActorsSchema.Items = ActorReferenceSchema;
	Schema.Add(TEXT("actors"), ActorsSchema);

	Schema.Add(TEXT("include_pairwise_distances"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include pairwise center distances")));
	Schema.Add(TEXT("include_ground_hits"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include ground-hit traces below each actor")));
	Schema.Add(TEXT("trace_distance"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Ground trace distance in world units")));
	return Schema;
}

FMcpToolResult UQuerySpatialContextTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bIncludePairwiseDistances = GetBoolArgOrDefault(Arguments, TEXT("include_pairwise_distances"), true);
	const bool bIncludeGroundHits = GetBoolArgOrDefault(Arguments, TEXT("include_ground_hits"), false);
	const double TraceDistance = FMath::Max(1.0, static_cast<double>(GetFloatArgOrDefault(Arguments, TEXT("trace_distance"), 100000.0f)));

	const TArray<TSharedPtr<FJsonValue>>* ActorReferences = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actors"), ActorReferences) || !ActorReferences || ActorReferences->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actors' array is required"));
	}

	UWorld* World = nullptr;
	TArray<AActor*> Actors;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	if (!LevelActorToolUtils::ResolveActorReferences(*ActorReferences, WorldType, Context, World, Actors, ErrorCode, ErrorMessage, ErrorDetails))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	for (AActor* Actor : Actors)
	{
		TSharedPtr<FJsonObject> ActorObject = McpV2ToolUtils::SerializeActorSummary(Actor, Context.SessionId, true, true);
		ActorObject->SetArrayField(TEXT("pivot_location"), VectorToArray(Actor->GetActorLocation()));
		ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObject)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World ? World->GetName() : TEXT(""));
	Result->SetStringField(TEXT("world_path"), World ? World->GetPathName() : TEXT(""));
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetObjectField(TEXT("aggregate_bounds"), McpV2ToolUtils::BuildAggregateBounds(Actors));

	if (bIncludePairwiseDistances)
	{
		TArray<TSharedPtr<FJsonValue>> PairwiseDistancesArray;
		for (int32 SourceIndex = 0; SourceIndex < Actors.Num(); ++SourceIndex)
		{
			for (int32 TargetIndex = SourceIndex + 1; TargetIndex < Actors.Num(); ++TargetIndex)
			{
				AActor* SourceActor = Actors[SourceIndex];
				AActor* TargetActor = Actors[TargetIndex];
				if (!SourceActor || !TargetActor)
				{
					continue;
				}

				TSharedPtr<FJsonObject> DistanceObject = MakeShareable(new FJsonObject);
				DistanceObject->SetNumberField(TEXT("from_index"), SourceIndex);
				DistanceObject->SetNumberField(TEXT("to_index"), TargetIndex);
				DistanceObject->SetStringField(TEXT("from_actor"), SourceActor->GetActorNameOrLabel());
				DistanceObject->SetStringField(TEXT("to_actor"), TargetActor->GetActorNameOrLabel());
				DistanceObject->SetNumberField(TEXT("distance"),
					FVector::Distance(SourceActor->GetActorLocation(), TargetActor->GetActorLocation()));
				PairwiseDistancesArray.Add(MakeShareable(new FJsonValueObject(DistanceObject)));
			}
		}
		Result->SetArrayField(TEXT("pairwise_distances"), PairwiseDistancesArray);
	}

	if (bIncludeGroundHits)
	{
		TArray<TSharedPtr<FJsonValue>> GroundHitsArray;
		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			AActor* Actor = Actors[ActorIndex];
			if (!Actor)
			{
				continue;
			}

			FHitResult HitResult;
			FVector BoundsOrigin = FVector::ZeroVector;
			FVector BoundsExtent = FVector::ZeroVector;
			const bool bHit = LevelActorToolUtils::TraceGroundBelowActor(
				World,
				Actor,
				TraceDistance,
				HitResult,
				&BoundsOrigin,
				&BoundsExtent);

			TSharedPtr<FJsonObject> HitObject = MakeShareable(new FJsonObject);
			HitObject->SetNumberField(TEXT("index"), ActorIndex);
			HitObject->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
			HitObject->SetBoolField(TEXT("hit"), bHit);
			HitObject->SetArrayField(TEXT("trace_start"), VectorToArray(BoundsOrigin + FVector(0.0, 0.0, BoundsExtent.Z + 10.0)));
			HitObject->SetArrayField(TEXT("trace_end"), VectorToArray(BoundsOrigin - FVector(0.0, 0.0, TraceDistance)));

			if (bHit)
			{
				HitObject->SetArrayField(TEXT("location"), VectorToArray(HitResult.ImpactPoint));
				HitObject->SetArrayField(TEXT("normal"), VectorToArray(HitResult.ImpactNormal));
				HitObject->SetStringField(TEXT("hit_actor"),
					HitResult.GetActor() ? HitResult.GetActor()->GetActorNameOrLabel() : TEXT(""));
				HitObject->SetNumberField(TEXT("distance"), HitResult.Distance);
			}

			GroundHitsArray.Add(MakeShareable(new FJsonValueObject(HitObject)));
		}
		Result->SetArrayField(TEXT("ground_hits"), GroundHitsArray);
	}

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Spatial context ready"));
}
