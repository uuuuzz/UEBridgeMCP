// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/AlignActorsBatchTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	struct FActorBoundsData
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
	};

	struct FSortedActorCenter
	{
		int32 Index = INDEX_NONE;
		double Center = 0.0;
	};

	bool TryParseAxisName(const FString& AxisName, int32& OutAxisIndex)
	{
		if (AxisName.Equals(TEXT("x"), ESearchCase::IgnoreCase))
		{
			OutAxisIndex = 0;
			return true;
		}
		if (AxisName.Equals(TEXT("y"), ESearchCase::IgnoreCase))
		{
			OutAxisIndex = 1;
			return true;
		}
		if (AxisName.Equals(TEXT("z"), ESearchCase::IgnoreCase))
		{
			OutAxisIndex = 2;
			return true;
		}
		return false;
	}

	double GetAxisComponent(const FVector& Vector, int32 AxisIndex)
	{
		switch (AxisIndex)
		{
		case 0:
			return Vector.X;
		case 1:
			return Vector.Y;
		default:
			return Vector.Z;
		}
	}

	void AddAxisDelta(FVector& InOutVector, int32 AxisIndex, double Delta)
	{
		switch (AxisIndex)
		{
		case 0:
			InOutVector.X += Delta;
			break;
		case 1:
			InOutVector.Y += Delta;
			break;
		default:
			InOutVector.Z += Delta;
			break;
		}
	}

	FActorBoundsData GetActorBoundsData(AActor* Actor)
	{
		FActorBoundsData Data;
		if (Actor)
		{
			Actor->GetActorBounds(false, Data.Origin, Data.Extent, false);
		}
		return Data;
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
}

FString UAlignActorsBatchTool::GetToolDescription() const
{
	return TEXT("Align or distribute actors using world-space actor bounds. Supports center/min/max alignment and center distribution.");
}

TMap<FString, FMcpSchemaProperty> UAlignActorsBatchTool::GetInputSchema() const
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
	ActorsSchema.Description = TEXT("Actors to align");
	ActorsSchema.bRequired = true;
	ActorsSchema.Items = ActorReferenceSchema;
	Schema.Add(TEXT("actors"), ActorsSchema);

	Schema.Add(TEXT("axis"), FMcpSchemaProperty::MakeEnum(TEXT("Alignment axis"), { TEXT("x"), TEXT("y"), TEXT("z") }, true));
	Schema.Add(TEXT("mode"), FMcpSchemaProperty::MakeEnum(
		TEXT("Alignment mode"),
		{ TEXT("align_min"), TEXT("align_center"), TEXT("align_max"), TEXT("distribute_centers") },
		true));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after edits. Ignored for PIE worlds.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back on first failure")));

	return Schema;
}

FMcpToolResult UAlignActorsBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* ActorReferences = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actors"), ActorReferences) || !ActorReferences || ActorReferences->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actors' array is required"));
	}

	FString AxisName;
	if (!Arguments->TryGetStringField(TEXT("axis"), AxisName))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'axis' is required"));
	}

	int32 AxisIndex = INDEX_NONE;
	if (!TryParseAxisName(AxisName, AxisIndex))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'axis' must be 'x', 'y', or 'z'"));
	}

	FString Mode;
	if (!Arguments->TryGetStringField(TEXT("mode"), Mode))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'mode' is required"));
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

	TArray<FActorBoundsData> BoundsByActor;
	BoundsByActor.Reserve(Actors.Num());
	for (AActor* Actor : Actors)
	{
		BoundsByActor.Add(GetActorBoundsData(Actor));
	}

	TArray<FTransform> AfterTransforms;
	AfterTransforms.Reserve(Actors.Num());
	for (AActor* Actor : Actors)
	{
		AfterTransforms.Add(Actor ? Actor->GetActorTransform() : FTransform::Identity);
	}

	if (Mode.Equals(TEXT("distribute_centers"), ESearchCase::IgnoreCase))
	{
		if (Actors.Num() >= 3)
		{
			TArray<FSortedActorCenter> SortedCenters;
			SortedCenters.Reserve(Actors.Num());
			for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
			{
				SortedCenters.Add({ ActorIndex, GetAxisComponent(BoundsByActor[ActorIndex].Origin, AxisIndex) });
			}

			SortedCenters.Sort([](const FSortedActorCenter& Left, const FSortedActorCenter& Right)
			{
				return Left.Center < Right.Center;
			});

			const double StartCenter = SortedCenters[0].Center;
			const double EndCenter = SortedCenters.Last().Center;
			const double Step = (EndCenter - StartCenter) / static_cast<double>(SortedCenters.Num() - 1);

			for (int32 SortedIndex = 1; SortedIndex < SortedCenters.Num() - 1; ++SortedIndex)
			{
				const int32 ActorIndex = SortedCenters[SortedIndex].Index;
				const double TargetCenter = StartCenter + (Step * static_cast<double>(SortedIndex));
				const double CurrentCenter = GetAxisComponent(BoundsByActor[ActorIndex].Origin, AxisIndex);
				const double Delta = TargetCenter - CurrentCenter;
				FVector NewLocation = AfterTransforms[ActorIndex].GetLocation();
				AddAxisDelta(NewLocation, AxisIndex, Delta);
				AfterTransforms[ActorIndex].SetLocation(NewLocation);
			}
		}
	}
	else
	{
		const FActorBoundsData AnchorBounds = BoundsByActor[0];
		const double AnchorMin = GetAxisComponent(AnchorBounds.Origin, AxisIndex) - GetAxisComponent(AnchorBounds.Extent, AxisIndex);
		const double AnchorCenter = GetAxisComponent(AnchorBounds.Origin, AxisIndex);
		const double AnchorMax = GetAxisComponent(AnchorBounds.Origin, AxisIndex) + GetAxisComponent(AnchorBounds.Extent, AxisIndex);

		for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			const FActorBoundsData& Bounds = BoundsByActor[ActorIndex];
			double Delta = 0.0;

			if (Mode.Equals(TEXT("align_min"), ESearchCase::IgnoreCase))
			{
				Delta = AnchorMin - (GetAxisComponent(Bounds.Origin, AxisIndex) - GetAxisComponent(Bounds.Extent, AxisIndex));
			}
			else if (Mode.Equals(TEXT("align_center"), ESearchCase::IgnoreCase))
			{
				Delta = AnchorCenter - GetAxisComponent(Bounds.Origin, AxisIndex);
			}
			else if (Mode.Equals(TEXT("align_max"), ESearchCase::IgnoreCase))
			{
				Delta = AnchorMax - (GetAxisComponent(Bounds.Origin, AxisIndex) + GetAxisComponent(Bounds.Extent, AxisIndex));
			}
			else
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"),
					TEXT("'mode' must be 'align_min', 'align_center', 'align_max', or 'distribute_centers'"));
			}

			FVector NewLocation = AfterTransforms[ActorIndex].GetLocation();
			AddAxisDelta(NewLocation, AxisIndex, Delta);
			AfterTransforms[ActorIndex].SetLocation(NewLocation);
		}
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
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Align Actors Batch")));
	}

	for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
	{
		AActor* Actor = Actors[ActorIndex];
		const FTransform BeforeTransform = Actor->GetActorTransform();
		const FTransform AfterTransform = AfterTransforms[ActorIndex];
		const bool bChanged = !BeforeTransform.Equals(AfterTransform);

		TSharedPtr<FJsonObject> ActorResult = MakeShareable(new FJsonObject);
		ActorResult->SetNumberField(TEXT("index"), ActorIndex);
		ActorResult->SetStringField(TEXT("axis"), AxisName.ToLower());
		ActorResult->SetStringField(TEXT("mode"), Mode);
		ActorResult->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(Actor, Context.SessionId, false, false));
		ActorResult->SetObjectField(TEXT("before_transform"), McpV2ToolUtils::SerializeTransform(BeforeTransform));
		ActorResult->SetObjectField(TEXT("after_transform"), McpV2ToolUtils::SerializeTransform(AfterTransform));
		ActorResult->SetBoolField(TEXT("changed"), bChanged);
		ActorResult->SetBoolField(TEXT("success"), true);

		if (!bDryRun && bChanged)
		{
			Actor->Modify();
			if (!Actor->SetActorTransform(AfterTransform))
			{
				ActorResult->SetBoolField(TEXT("success"), false);
				ActorResult->SetStringField(TEXT("error"), TEXT("Failed to apply aligned transform"));
				bAnyFailed = true;
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));

				if (bRollbackOnError)
				{
					Transaction.Reset();
					return FMcpToolResult::StructuredError(
						TEXT("UEBMCP_OPERATION_FAILED"),
						TEXT("Failed to apply aligned transform"),
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
