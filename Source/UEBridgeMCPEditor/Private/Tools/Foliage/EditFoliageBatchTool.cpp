// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Foliage/EditFoliageBatchTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "ScopedTransaction.h"

namespace
{
	struct FFoliageResolvedTarget
	{
		UFoliageType* FoliageType = nullptr;
		FFoliageInfo* FoliageInfo = nullptr;
		FString SourcePath;
	};

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

	bool TryReadTransformObject(const TSharedPtr<FJsonObject>& Object, FTransform& OutTransform)
	{
		FVector Location = FVector::ZeroVector;
		FVector RotationVector = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		TryReadVectorField(Object, TEXT("location"), Location);
		TryReadVectorField(Object, TEXT("rotation"), RotationVector);
		TryReadVectorField(Object, TEXT("scale"), Scale);
		OutTransform = FTransform(FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z), Location, Scale);
		return true;
	}

	bool ResolveFoliageTarget(
		AInstancedFoliageActor* FoliageActor,
		const TSharedPtr<FJsonObject>& OperationObject,
		bool bCreateIfMissing,
		bool bDryRun,
		FFoliageResolvedTarget& OutTarget,
		FString& OutError)
	{
		if (!OperationObject.IsValid())
		{
			OutError = TEXT("Invalid foliage target context");
			return false;
		}

		if (!FoliageActor && !(bDryRun && bCreateIfMissing))
		{
			OutError = TEXT("Unable to resolve an InstancedFoliageActor for the current level");
			return false;
		}

		FString FoliageTypePath;
		FString MeshPath;
		OperationObject->TryGetStringField(TEXT("foliage_type_path"), FoliageTypePath);
		OperationObject->TryGetStringField(TEXT("mesh_path"), MeshPath);

		if (!FoliageTypePath.IsEmpty())
		{
			UFoliageType* FoliageType = FMcpAssetModifier::LoadAssetByPath<UFoliageType>(FoliageTypePath, OutError);
			if (!FoliageType)
			{
				return false;
			}

			FFoliageInfo* FoliageInfo = FoliageActor ? FoliageActor->FindInfo(FoliageType) : nullptr;
			if (!FoliageInfo && bCreateIfMissing)
			{
				if (bDryRun)
				{
					OutTarget.FoliageType = FoliageType;
					OutTarget.FoliageInfo = nullptr;
					OutTarget.SourcePath = FoliageTypePath;
					return true;
				}

				if (FoliageActor->GetWorld() && FoliageActor->GetWorld()->IsPartitionedWorld() && !FoliageType->IsAsset())
				{
					OutError = TEXT("World Partition foliage requires foliage_type_path to reference a saved UFoliageType asset");
					return false;
				}

				FoliageType = FoliageActor->AddFoliageType(FoliageType, &FoliageInfo);
			}

			if (!FoliageInfo)
			{
				OutError = FString::Printf(TEXT("Foliage type '%s' is not present in the current level"), *FoliageTypePath);
				return false;
			}

			OutTarget.FoliageType = FoliageType;
			OutTarget.FoliageInfo = FoliageInfo;
			OutTarget.SourcePath = FoliageTypePath;
			return true;
		}

		if (MeshPath.IsEmpty())
		{
			OutError = TEXT("Either 'foliage_type_path' or 'mesh_path' is required");
			return false;
		}

		UStaticMesh* StaticMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(MeshPath, OutError);
		if (!StaticMesh)
		{
			return false;
		}

		if (FoliageActor)
		{
			for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : FoliageActor->GetFoliageInfos())
			{
				UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Pair.Key);
				if (MeshType && MeshType->GetStaticMesh() == StaticMesh)
				{
					OutTarget.FoliageType = MeshType;
					OutTarget.FoliageInfo = const_cast<FFoliageInfo*>(&Pair.Value.Get());
					OutTarget.SourcePath = MeshPath;
					return true;
				}
			}
		}

		if (!bCreateIfMissing)
		{
			OutError = FString::Printf(TEXT("No foliage info found for mesh '%s'"), *MeshPath);
			return false;
		}

		if (bDryRun)
		{
			OutTarget.FoliageType = nullptr;
			OutTarget.FoliageInfo = nullptr;
			OutTarget.SourcePath = MeshPath;
			return true;
		}

		if (FoliageActor->GetWorld() && FoliageActor->GetWorld()->IsPartitionedWorld())
		{
			OutError = TEXT("World Partition foliage cannot auto-create a foliage type from mesh_path; create or pass a saved foliage_type_path instead");
			return false;
		}

		UFoliageType* CreatedType = nullptr;
		FFoliageInfo* CreatedInfo = FoliageActor->AddMesh(StaticMesh, &CreatedType);
		if (!CreatedType || !CreatedInfo)
		{
			OutError = FString::Printf(TEXT("Failed to create foliage info for mesh '%s'"), *MeshPath);
			return false;
		}

		OutTarget.FoliageType = CreatedType;
		OutTarget.FoliageInfo = CreatedInfo;
		OutTarget.SourcePath = MeshPath;
		return true;
	}
}

FString UEditFoliageBatchTool::GetToolDescription() const
{
	return TEXT("Batch edit foliage instances in the current level, including add/remove, transform updates, and foliage type mesh replacement.");
}

TMap<FString, FMcpSchemaProperty> UEditFoliageBatchTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to edit"), { TEXT("editor"), TEXT("pie") }));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Foliage edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Foliage edit action"),
		{ TEXT("add_instances"), TEXT("remove_instances_in_bounds"), TEXT("set_instance_transforms"), TEXT("replace_foliage_type_mesh") },
		true)));
	OperationSchema->Properties.Add(TEXT("foliage_type_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional foliage type asset path"))));
	OperationSchema->Properties.Add(TEXT("mesh_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional source static mesh path"))));
	OperationSchema->Properties.Add(TEXT("new_mesh_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Replacement static mesh path for replace_foliage_type_mesh"))));
	OperationSchema->Properties.Add(TEXT("transforms"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("array"), TEXT("Transforms to add or apply"))));
	OperationSchema->Properties.Add(TEXT("instances"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("array"), TEXT("Instance transform updates"))));
	OperationSchema->Properties.Add(TEXT("bounds_min"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Bounds min [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("bounds_max"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Bounds max [x,y,z]"), TEXT("number"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Foliage edit operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save changed assets and world")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on the first failure")));
	return Schema;
}

FMcpToolResult UEditFoliageBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
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

	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the requested world"));
	}

	AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::Get(World, !bDryRun);
	if (!FoliageActor && !bDryRun)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_FOLIAGE_NOT_AVAILABLE"), TEXT("Unable to resolve an InstancedFoliageActor for the current level"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeShareable(new FScopedTransaction(FText::FromString(TEXT("Edit Foliage Batch"))));
		if (FoliageActor)
		{
			FoliageActor->Modify();
		}
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	TArray<UObject*> AssetsToSave;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		const FString Action = GetStringArgOrDefault(*OperationObject, TEXT("action")).ToLower();
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		FFoliageResolvedTarget ResolvedTarget;
		FString ResolveError;
		const bool bCreateIfMissing = Action == TEXT("add_instances");
		if (!ResolveFoliageTarget(FoliageActor, *OperationObject, bCreateIfMissing, bDryRun, ResolvedTarget, ResolveError))
		{
			ResultObject->SetStringField(TEXT("error"), ResolveError);
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		ResultObject->SetStringField(TEXT("source_path"), ResolvedTarget.SourcePath);
		ResultObject->SetBoolField(TEXT("changed"), false);

		if (Action == TEXT("add_instances"))
		{
			const TArray<TSharedPtr<FJsonValue>>* TransformValues = nullptr;
			if (!(*OperationObject)->TryGetArrayField(TEXT("transforms"), TransformValues) || !TransformValues)
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("'transforms' array is required for add_instances"));
			}
			else
			{
				int32 AddedCount = 0;
				for (const TSharedPtr<FJsonValue>& TransformValue : *TransformValues)
				{
					const TSharedPtr<FJsonObject>* TransformObject = nullptr;
					if (!TransformValue.IsValid() || !TransformValue->TryGetObject(TransformObject) || !TransformObject || !(*TransformObject).IsValid())
					{
						continue;
					}

					FTransform InstanceTransform;
					TryReadTransformObject(*TransformObject, InstanceTransform);
					FFoliageInstance NewInstance;
					NewInstance.SetInstanceWorldTransform(InstanceTransform);

					if (!bDryRun)
					{
						ResolvedTarget.FoliageInfo->AddInstance(ResolvedTarget.FoliageType, NewInstance);
					}
					++AddedCount;
				}

				ResultObject->SetNumberField(TEXT("added_count"), AddedCount);
				ResultObject->SetBoolField(TEXT("changed"), AddedCount > 0);
				bAnyChanged |= AddedCount > 0;
			}
		}
		else if (Action == TEXT("remove_instances_in_bounds"))
		{
			FVector BoundsMin = FVector::ZeroVector;
			FVector BoundsMax = FVector::ZeroVector;
			if (!TryReadVectorField(*OperationObject, TEXT("bounds_min"), BoundsMin) || !TryReadVectorField(*OperationObject, TEXT("bounds_max"), BoundsMax))
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("'bounds_min' and 'bounds_max' are required for remove_instances_in_bounds"));
			}
			else
			{
				TArray<int32> Indices;
				ResolvedTarget.FoliageInfo->GetInstancesInsideBounds(FBox(BoundsMin, BoundsMax), Indices);
				if (!bDryRun && Indices.Num() > 0)
				{
					ResolvedTarget.FoliageInfo->RemoveInstances(Indices, true);
				}
				ResultObject->SetNumberField(TEXT("removed_count"), Indices.Num());
				ResultObject->SetBoolField(TEXT("changed"), Indices.Num() > 0);
				bAnyChanged |= Indices.Num() > 0;
			}
		}
		else if (Action == TEXT("set_instance_transforms"))
		{
			const TArray<TSharedPtr<FJsonValue>>* InstanceValues = nullptr;
			if (!(*OperationObject)->TryGetArrayField(TEXT("instances"), InstanceValues) || !InstanceValues)
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("'instances' array is required for set_instance_transforms"));
			}
			else
			{
				TArray<int32> ModifiedIndices;
				for (const TSharedPtr<FJsonValue>& InstanceValue : *InstanceValues)
				{
					const TSharedPtr<FJsonObject>* InstanceObject = nullptr;
					if (!InstanceValue.IsValid() || !InstanceValue->TryGetObject(InstanceObject) || !InstanceObject || !(*InstanceObject).IsValid())
					{
						continue;
					}

					const int32 InstanceIndex = GetIntArgOrDefault(*InstanceObject, TEXT("index"), INDEX_NONE);
					if (!ResolvedTarget.FoliageInfo->Instances.IsValidIndex(InstanceIndex))
					{
						continue;
					}

					FTransform InstanceTransform = ResolvedTarget.FoliageInfo->Instances[InstanceIndex].GetInstanceWorldTransform();
					TryReadTransformObject(*InstanceObject, InstanceTransform);
					ModifiedIndices.Add(InstanceIndex);

					if (!bDryRun)
					{
						ResolvedTarget.FoliageInfo->SetInstanceWorldTransform(InstanceIndex, InstanceTransform, true);
					}
				}

				ResultObject->SetNumberField(TEXT("updated_count"), ModifiedIndices.Num());
				ResultObject->SetBoolField(TEXT("changed"), ModifiedIndices.Num() > 0);
				bAnyChanged |= ModifiedIndices.Num() > 0;
			}
		}
		else if (Action == TEXT("replace_foliage_type_mesh"))
		{
			UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(ResolvedTarget.FoliageType);
			if (!MeshType)
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("replace_foliage_type_mesh requires a UFoliageType_InstancedStaticMesh"));
			}
			else
			{
				FString LoadError;
				const FString NewMeshPath = GetStringArgOrDefault(*OperationObject, TEXT("new_mesh_path"));
				UStaticMesh* NewMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(NewMeshPath, LoadError);
				if (!NewMesh)
				{
					ResultObject->SetStringField(TEXT("error"), LoadError);
				}
				else
				{
					if (!bDryRun)
					{
						if (ResolvedTarget.FoliageInfo)
						{
							ResolvedTarget.FoliageInfo->NotifyFoliageTypeWillChange(MeshType);
						}
						MeshType->Modify();
						MeshType->SetStaticMesh(NewMesh);
						if (ResolvedTarget.FoliageInfo)
						{
							ResolvedTarget.FoliageInfo->NotifyFoliageTypeChanged(MeshType, true);
						}
						FMcpAssetModifier::MarkPackageDirty(MeshType);
						AssetsToSave.AddUnique(MeshType);
						ModifiedAssetsArray.Add(MakeShareable(new FJsonValueObject(McpV2ToolUtils::MakeAssetHandle(MeshType->GetPathName(), MeshType->GetClass()->GetName()))));
					}

					ResultObject->SetStringField(TEXT("new_mesh_path"), NewMeshPath);
					ResultObject->SetBoolField(TEXT("changed"), true);
					bAnyChanged = true;
				}
			}
		}
		else
		{
			ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported action: '%s'"), *Action));
		}

		if (ResultObject->HasField(TEXT("error")))
		{
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		if (bAnyFailed && bRollbackOnError)
		{
			break;
		}
	}

	if (!bDryRun && bAnyChanged)
	{
		LevelActorToolUtils::AppendWorldModifiedAsset(World, ModifiedAssetsArray);
	}

	for (UObject* AssetToSave : AssetsToSave)
	{
		if (!AssetToSave || !bSave || bDryRun)
		{
			continue;
		}

		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(AssetToSave, false, SaveError))
		{
			TSharedPtr<FJsonObject> WarningObject = MakeShareable(new FJsonObject);
			WarningObject->SetStringField(TEXT("message"), SaveError);
			WarningsArray.Add(MakeShareable(new FJsonValueObject(WarningObject)));
		}
	}

	FString SaveErrorCode;
	FString SaveErrorMessage;
	if (!bDryRun && bAnyChanged && !LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, WarningsArray, ModifiedAssetsArray, SaveErrorCode, SaveErrorMessage))
	{
		return FMcpToolResult::StructuredError(SaveErrorCode, SaveErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
