// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/DropActorsToSurfaceTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/RotationMatrix.h"

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

	TSharedPtr<FJsonObject> BuildPartialFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
		PartialResult->SetStringField(TEXT("tool"), ToolName);
		PartialResult->SetArrayField(TEXT("results"), ResultsArray);
		PartialResult->SetArrayField(TEXT("warnings"), WarningsArray);
		PartialResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		PartialResult->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return PartialResult;
	}

	FQuat BuildAlignedNormalRotation(const FRotator& CurrentRotation, const FVector& SurfaceNormal)
	{
		const FVector DesiredUp = SurfaceNormal.GetSafeNormal();
		FVector Forward = FVector::VectorPlaneProject(FRotator(0.0, CurrentRotation.Yaw, 0.0).Vector(), DesiredUp).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::VectorPlaneProject(FRotator(0.0, CurrentRotation.Yaw + 90.0, 0.0).Vector(), DesiredUp).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::CrossProduct(FVector::RightVector, DesiredUp).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}

		return FRotationMatrix::MakeFromXZ(Forward, DesiredUp).ToQuat();
	}
}

FString UDropActorsToSurfaceTool::GetToolDescription() const
{
	return TEXT("Trace downward from each actor and move it so the actor bounds rest on the first hit surface, with optional normal alignment.");
}

TMap<FString, FMcpSchemaProperty> UDropActorsToSurfaceTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));

	TSharedPtr<FMcpSchemaProperty> ActorReferenceSchema = MakeShared<FMcpSchemaProperty>();
	ActorReferenceSchema->Type = TEXT("object");
	ActorReferenceSchema->Description = TEXT("Actor reference using either actor_name or actor_handle");
	ActorReferenceSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label"))));
	ActorReferenceSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle returned by query-level-summary or query-actor-selection"))));

	FMcpSchemaProperty ActorsSchema;
	ActorsSchema.Type = TEXT("array");
	ActorsSchema.Description = TEXT("Actors to drop");
	ActorsSchema.bRequired = true;
	ActorsSchema.Items = ActorReferenceSchema;
	Schema.Add(TEXT("actors"), ActorsSchema);

	Schema.Add(TEXT("trace_distance"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Ground trace distance in world units")));
	Schema.Add(TEXT("offset"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Additional offset above the hit surface")));
	Schema.Add(TEXT("align_to_normal"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Align the actor up-vector to the hit normal without changing yaw intent")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after edits. Ignored for PIE worlds.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back on first failure")));

	return Schema;
}

FMcpToolResult UDropActorsToSurfaceTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const double TraceDistance = FMath::Max(1.0, static_cast<double>(GetFloatArgOrDefault(Arguments, TEXT("trace_distance"), 100000.0f)));
	const double Offset = static_cast<double>(GetFloatArgOrDefault(Arguments, TEXT("offset"), 0.0f));
	const bool bAlignToNormal = GetBoolArgOrDefault(Arguments, TEXT("align_to_normal"), false);

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

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Drop Actors To Surface")));
	}

	for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
	{
		AActor* Actor = Actors[ActorIndex];
		const FTransform BeforeTransform = Actor->GetActorTransform();
		FTransform AfterTransform = BeforeTransform;

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

		TSharedPtr<FJsonObject> ActorResult = MakeShareable(new FJsonObject);
		ActorResult->SetNumberField(TEXT("index"), ActorIndex);
		ActorResult->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(Actor, Context.SessionId, false, false));
		ActorResult->SetBoolField(TEXT("hit"), bHit);
		ActorResult->SetObjectField(TEXT("before_transform"), McpV2ToolUtils::SerializeTransform(BeforeTransform));

		if (!bHit)
		{
			ActorResult->SetObjectField(TEXT("after_transform"), McpV2ToolUtils::SerializeTransform(AfterTransform));
			ActorResult->SetBoolField(TEXT("changed"), false);
			ActorResult->SetBoolField(TEXT("success"), true);
			WarningsArray.Add(MakeShareable(new FJsonValueString(
				FString::Printf(TEXT("No surface hit was found below actor '%s' within trace distance %.2f."), *Actor->GetActorNameOrLabel(), TraceDistance))));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));
			continue;
		}

		const double CurrentBottom = static_cast<double>(BoundsOrigin.Z - BoundsExtent.Z);
		const double TargetBottom = static_cast<double>(HitResult.ImpactPoint.Z) + Offset;
		const double DeltaZ = TargetBottom - CurrentBottom;

		FVector NewLocation = BeforeTransform.GetLocation();
		NewLocation.Z += DeltaZ;
		AfterTransform.SetLocation(NewLocation);

		if (bAlignToNormal)
		{
			AfterTransform.SetRotation(BuildAlignedNormalRotation(BeforeTransform.Rotator(), HitResult.ImpactNormal));
		}

		const bool bChanged = !BeforeTransform.Equals(AfterTransform);
		ActorResult->SetArrayField(TEXT("hit_location"), VectorToArray(HitResult.ImpactPoint));
		ActorResult->SetArrayField(TEXT("hit_normal"), VectorToArray(HitResult.ImpactNormal));
		ActorResult->SetStringField(TEXT("hit_actor"), HitResult.GetActor() ? HitResult.GetActor()->GetActorNameOrLabel() : TEXT(""));
		ActorResult->SetObjectField(TEXT("after_transform"), McpV2ToolUtils::SerializeTransform(AfterTransform));
		ActorResult->SetBoolField(TEXT("changed"), bChanged);
		ActorResult->SetBoolField(TEXT("success"), true);

		if (!bDryRun && bChanged)
		{
			Actor->Modify();
			if (!Actor->SetActorTransform(AfterTransform))
			{
				ActorResult->SetBoolField(TEXT("success"), false);
				ActorResult->SetStringField(TEXT("error"), TEXT("Failed to apply dropped transform"));
				bAnyFailed = true;
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));

				if (bRollbackOnError)
				{
					Transaction.Reset();
					return FMcpToolResult::StructuredError(
						TEXT("UEBMCP_OPERATION_FAILED"),
						TEXT("Failed to apply dropped transform"),
						nullptr,
						BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
				}
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));
	}

	if (!LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, WarningsArray, ModifiedAssetsArray, ErrorCode, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("world_name"), World ? World->GetName() : TEXT(""));
	Response->SetStringField(TEXT("world_path"), World ? World->GetPathName() : TEXT(""));
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
