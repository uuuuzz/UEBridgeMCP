// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/World/EditSplineActorsTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpV2ToolUtils.h"

#include "Components/SplineComponent.h"
#include "ScopedTransaction.h"

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

	bool TryReadVectorField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() < 3)
		{
			return false;
		}

		OutVector.X = static_cast<float>((*Values)[0]->AsNumber());
		OutVector.Y = static_cast<float>((*Values)[1]->AsNumber());
		OutVector.Z = static_cast<float>((*Values)[2]->AsNumber());
		return true;
	}

	bool TryReadRotatorField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FRotator& OutRotator)
	{
		FVector RotationVector;
		if (!TryReadVectorField(Object, FieldName, RotationVector))
		{
			return false;
		}

		OutRotator = FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z);
		return true;
	}

	USplineComponent* ResolveSplineComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TArray<USplineComponent*> SplineComponents;
		Actor->GetComponents<USplineComponent>(SplineComponents);
		if (SplineComponents.Num() == 0)
		{
			return nullptr;
		}

		if (ComponentName.IsEmpty())
		{
			return SplineComponents[0];
		}

		for (USplineComponent* SplineComponent : SplineComponents)
		{
			if (SplineComponent && SplineComponent->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return SplineComponent;
			}
		}

		return nullptr;
	}
}

FString UEditSplineActorsTool::GetToolDescription() const
{
	return TEXT("Batch edit spline components on actor instances, including point creation, removal, transform, tangents, and closed-loop state.");
}

TMap<FString, FMcpSchemaProperty> UEditSplineActorsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to edit"), { TEXT("editor"), TEXT("pie") }));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Spline edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Spline edit action"),
		{ TEXT("add_point"), TEXT("remove_point"), TEXT("set_point_transform"), TEXT("set_point_tangents"), TEXT("set_closed_loop") },
		true)));
	OperationSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name"))));
	OperationSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle"))));
	OperationSchema->Properties.Add(TEXT("component_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional spline component name"))));
	OperationSchema->Properties.Add(TEXT("index"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Spline point index"))));
	OperationSchema->Properties.Add(TEXT("position"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Point position [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("rotation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Point rotation [pitch,yaw,roll]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("scale"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Point scale [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("arrive_tangent"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Arrive tangent [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("leave_tangent"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Leave tangent [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("closed_loop"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Closed loop state"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Spline edit operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited world when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on the first failure")));
	return Schema;
}

FMcpToolResult UEditSplineActorsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeShareable(new FScopedTransaction(FText::FromString(TEXT("Edit Spline Actors"))));
	}

	UWorld* EditedWorld = nullptr;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		ResultObject->SetStringField(TEXT("action"), Action);

		UWorld* ResolvedWorld = nullptr;
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ErrorDetails;
		AActor* Actor = LevelActorToolUtils::ResolveActorReference(
			*OperationObject,
			RequestedWorldType,
			TEXT("actor_name"),
			TEXT("actor_handle"),
			Context,
			ResolvedWorld,
			ErrorCode,
			ErrorMessage,
			ErrorDetails,
			true);

		bool bOperationSuccess = false;
		bool bOperationChanged = false;
		if (!Actor)
		{
			ResultObject->SetStringField(TEXT("error"), ErrorMessage);
		}
		else
		{
			EditedWorld = EditedWorld ? EditedWorld : ResolvedWorld;
			const FString ComponentName = GetStringArgOrDefault(*OperationObject, TEXT("component_name"));
			USplineComponent* SplineComponent = ResolveSplineComponent(Actor, ComponentName);
			if (!SplineComponent)
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("Spline component not found on target actor"));
			}
			else
			{
				const bool bClosedLoopBefore = SplineComponent->IsClosedLoop();
				const int32 PointCountBefore = SplineComponent->GetNumberOfSplinePoints();

				if (!bDryRun)
				{
					Actor->Modify();
					SplineComponent->Modify();
				}

				if (Action == TEXT("add_point"))
				{
					FVector Position = FVector::ZeroVector;
					FRotator Rotation = FRotator::ZeroRotator;
					FVector Scale = FVector::OneVector;
					TryReadVectorField(*OperationObject, TEXT("position"), Position);
					TryReadRotatorField(*OperationObject, TEXT("rotation"), Rotation);
					TryReadVectorField(*OperationObject, TEXT("scale"), Scale);

					if (!bDryRun)
					{
						FSplinePoint NewPoint(static_cast<float>(PointCountBefore), Position, ESplinePointType::Curve, Rotation, Scale);
						SplineComponent->AddPoint(NewPoint, false);
						SplineComponent->UpdateSpline();
					}
					bOperationSuccess = true;
					bOperationChanged = true;
				}
				else if (Action == TEXT("remove_point"))
				{
					const int32 PointIndex = GetIntArgOrDefault(*OperationObject, TEXT("index"), INDEX_NONE);
					if (PointIndex < 0 || PointIndex >= PointCountBefore)
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("Valid 'index' is required for remove_point"));
					}
					else
					{
						if (!bDryRun)
						{
							SplineComponent->RemoveSplinePoint(PointIndex, false);
							SplineComponent->UpdateSpline();
						}
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
				else if (Action == TEXT("set_point_transform"))
				{
					const int32 PointIndex = GetIntArgOrDefault(*OperationObject, TEXT("index"), INDEX_NONE);
					if (PointIndex < 0 || PointIndex >= PointCountBefore)
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("Valid 'index' is required for set_point_transform"));
					}
					else
					{
						FVector Position = SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
						FRotator Rotation = SplineComponent->GetRotationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
						FVector Scale = SplineComponent->GetScaleAtSplinePoint(PointIndex);
						TryReadVectorField(*OperationObject, TEXT("position"), Position);
						TryReadRotatorField(*OperationObject, TEXT("rotation"), Rotation);
						TryReadVectorField(*OperationObject, TEXT("scale"), Scale);

						if (!bDryRun)
						{
							SplineComponent->SetLocationAtSplinePoint(PointIndex, Position, ESplineCoordinateSpace::World, false);
							SplineComponent->SetRotationAtSplinePoint(PointIndex, Rotation, ESplineCoordinateSpace::World, false);
							SplineComponent->SetScaleAtSplinePoint(PointIndex, Scale, false);
							SplineComponent->UpdateSpline();
						}
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
				else if (Action == TEXT("set_point_tangents"))
				{
					const int32 PointIndex = GetIntArgOrDefault(*OperationObject, TEXT("index"), INDEX_NONE);
					FVector ArriveTangent = FVector::ZeroVector;
					FVector LeaveTangent = FVector::ZeroVector;
					if (PointIndex < 0 || PointIndex >= PointCountBefore ||
						!TryReadVectorField(*OperationObject, TEXT("arrive_tangent"), ArriveTangent) ||
						!TryReadVectorField(*OperationObject, TEXT("leave_tangent"), LeaveTangent))
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("Valid 'index', 'arrive_tangent', and 'leave_tangent' are required for set_point_tangents"));
					}
					else
					{
						if (!bDryRun)
						{
							SplineComponent->SetTangentsAtSplinePoint(PointIndex, ArriveTangent, LeaveTangent, ESplineCoordinateSpace::World, false);
							SplineComponent->UpdateSpline();
						}
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
				else if (Action == TEXT("set_closed_loop"))
				{
					bool bClosedLoop = false;
					if (!(*OperationObject)->TryGetBoolField(TEXT("closed_loop"), bClosedLoop))
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("'closed_loop' is required for set_closed_loop"));
					}
					else
					{
						if (!bDryRun)
						{
							SplineComponent->SetClosedLoop(bClosedLoop, false);
							SplineComponent->UpdateSpline();
						}
						bOperationSuccess = true;
						bOperationChanged = bClosedLoopBefore != bClosedLoop;
					}
				}
				else
				{
					ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported action: '%s'"), *Action));
				}

				ResultObject->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
				ResultObject->SetStringField(TEXT("component_name"), SplineComponent->GetName());
				ResultObject->SetNumberField(TEXT("point_count_before"), PointCountBefore);
				ResultObject->SetNumberField(TEXT("point_count_after"), bDryRun ? PointCountBefore : SplineComponent->GetNumberOfSplinePoints());
			}
		}

		if (!ResultObject->HasField(TEXT("error")))
		{
			ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
			ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
		}
		else
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetBoolField(TEXT("changed"), false);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));

			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					ResultObject->GetStringField(TEXT("error")),
					nullptr,
					GameplayToolUtils::BuildBatchFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}

		bAnyChanged = bAnyChanged || bOperationChanged;
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bAnyChanged && EditedWorld)
	{
		FString ErrorCode;
		FString ErrorMessage;
		if (!LevelActorToolUtils::SaveWorldIfNeeded(EditedWorld, bSave, WarningsArray, ModifiedAssetsArray, ErrorCode, ErrorMessage))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
