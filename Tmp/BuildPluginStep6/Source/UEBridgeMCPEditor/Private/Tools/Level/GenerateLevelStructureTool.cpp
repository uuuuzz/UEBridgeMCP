// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/GenerateLevelStructureTool.h"
#include "Utils/McpAssetModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Materials/MaterialInterface.h"

namespace GenerateLevelStructureToolPrivate
{
	TArray<TSharedPtr<FJsonValue>> MakeVectorArray(const FVector& InVector)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShareable(new FJsonValueNumber(InVector.X)));
		Values.Add(MakeShareable(new FJsonValueNumber(InVector.Y)));
		Values.Add(MakeShareable(new FJsonValueNumber(InVector.Z)));
		return Values;
	}

	TSharedPtr<FJsonObject> BuildWorldPayload(UWorld* World)
	{
		TSharedPtr<FJsonObject> WorldObject = MakeShareable(new FJsonObject);
		WorldObject->SetStringField(TEXT("type"), TEXT("editor"));
		WorldObject->SetStringField(TEXT("map_name"), World ? World->GetMapName() : TEXT(""));
		if (World && World->PersistentLevel && World->PersistentLevel->GetPackage())
		{
			WorldObject->SetStringField(TEXT("package_name"), World->PersistentLevel->GetPackage()->GetName());
		}
		return WorldObject;
	}

	void AddModifiedWorldAsset(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		if (!World || !World->PersistentLevel || !World->PersistentLevel->GetPackage())
		{
			return;
		}

		const FString PackageName = World->PersistentLevel->GetPackage()->GetName();
		if (!PackageName.IsEmpty())
		{
			OutModifiedAssets.Add(MakeShareable(new FJsonValueString(PackageName)));
		}
	}

	TSharedPtr<FJsonObject> BuildPartialResult(
		const FString& StructureType,
		UWorld* World,
		const TArray<TSharedPtr<FJsonValue>>& SpawnedActors,
		const TArray<TSharedPtr<FJsonValue>>& Warnings,
		const TArray<TSharedPtr<FJsonValue>>& Diagnostics,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssets)
	{
		TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
		PartialResult->SetStringField(TEXT("tool"), TEXT("generate-level-structure"));
		PartialResult->SetBoolField(TEXT("success"), false);
		PartialResult->SetStringField(TEXT("structure_type"), StructureType);
		PartialResult->SetArrayField(TEXT("spawned_actors"), SpawnedActors);
		PartialResult->SetArrayField(TEXT("warnings"), Warnings);
		PartialResult->SetArrayField(TEXT("diagnostics"), Diagnostics);
		PartialResult->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
		PartialResult->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
		PartialResult->SetObjectField(TEXT("world"), BuildWorldPayload(World));

		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetNumberField(TEXT("total_spawned"), SpawnedActors.Num());
		Summary->SetBoolField(TEXT("saved"), false);
		PartialResult->SetObjectField(TEXT("summary"), Summary);
		return PartialResult;
	}
}

FString UGenerateLevelStructureTool::GetToolDescription() const
{
	return TEXT("Generate procedural level structures in the editor world using the engine Cube mesh. "
		"Supports dry-run validation, optional material overrides by generated part name, and level saving.");
}

TMap<FString, FMcpSchemaProperty> UGenerateLevelStructureTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("structure_type"), FMcpSchemaProperty::MakeEnum(
		TEXT("Type of structure to generate"),
		{TEXT("box_room"), TEXT("corridor"), TEXT("staircase"), TEXT("platform"), TEXT("wall")}));

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("Target world. Only 'editor' is currently supported."),
		{TEXT("editor")}));

	FMcpSchemaProperty OriginSchema;
	OriginSchema.Type = TEXT("array");
	OriginSchema.Description = TEXT("Origin point [x, y, z]");
	OriginSchema.ItemsType = TEXT("number");
	Schema.Add(TEXT("origin"), OriginSchema);

	FMcpSchemaProperty SizeSchema;
	SizeSchema.Type = TEXT("object");
	SizeSchema.Description = TEXT("Size: {width, depth, height} in UE units");
	SizeSchema.NestedRequired = {TEXT("width"), TEXT("depth"), TEXT("height")};
	SizeSchema.Properties.Add(TEXT("width"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Structure width in UE units"), true)));
	SizeSchema.Properties.Add(TEXT("depth"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Structure depth in UE units"), true)));
	SizeSchema.Properties.Add(TEXT("height"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Structure height in UE units"), true)));
	Schema.Add(TEXT("size"), SizeSchema);

	TSharedPtr<FJsonObject> MaterialOverridesRawSchema = MakeShareable(new FJsonObject);
	MaterialOverridesRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	MaterialOverridesRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> MaterialOverridesSchema = MakeShared<FMcpSchemaProperty>();
	MaterialOverridesSchema->Description = TEXT("Optional material overrides keyed by generated part name. Values are material asset paths.");
	MaterialOverridesSchema->RawSchema = MaterialOverridesRawSchema;
	Schema.Add(TEXT("material_overrides"), *MaterialOverridesSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after generation")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate the request only without spawning actors")));

	return Schema;
}

TArray<FString> UGenerateLevelStructureTool::GetRequiredParams() const
{
	return {TEXT("structure_type")};
}

FMcpToolResult UGenerateLevelStructureTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString StructureType;
	if (!GetStringArg(Arguments, TEXT("structure_type"), StructureType))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'structure_type' required"));
	}

	const FString RequestedWorld = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	if (!RequestedWorld.IsEmpty() && !RequestedWorld.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSUPPORTED_COMBINATION"), TEXT("generate-level-structure currently only supports world='editor'"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const TSharedPtr<FJsonObject>* MaterialOverridesObject = nullptr;
	Arguments->TryGetObjectField(TEXT("material_overrides"), MaterialOverridesObject);

	FVector Origin = FVector::ZeroVector;
	const TArray<TSharedPtr<FJsonValue>>* OriginArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("origin"), OriginArray) && OriginArray && OriginArray->Num() >= 3)
	{
		Origin = FVector((*OriginArray)[0]->AsNumber(), (*OriginArray)[1]->AsNumber(), (*OriginArray)[2]->AsNumber());
	}

	double Width = 500.0;
	double Depth = 500.0;
	double Height = 300.0;
	const TSharedPtr<FJsonObject>* SizeObject = nullptr;
	if (Arguments->TryGetObjectField(TEXT("size"), SizeObject) && SizeObject && (*SizeObject).IsValid())
	{
		(*SizeObject)->TryGetNumberField(TEXT("width"), Width);
		(*SizeObject)->TryGetNumberField(TEXT("depth"), Depth);
		(*SizeObject)->TryGetNumberField(TEXT("height"), Height);
	}

	if (Width <= 0.0 || Depth <= 0.0 || Height <= 0.0)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetNumberField(TEXT("width"), Width);
		Details->SetNumberField(TEXT("depth"), Depth);
		Details->SetNumberField(TEXT("height"), Height);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("size.width, size.depth, and size.height must all be greater than zero"), Details);
	}

	if (!GEditor)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("Editor is not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No editor world is currently available"));
	}

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	TArray<TSharedPtr<FJsonValue>> SpawnedActorsArray;

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetBoolField(TEXT("dry_run"), true);
		Summary->SetBoolField(TEXT("save_requested"), bSave);
		Summary->SetNumberField(TEXT("total_spawned"), 0);

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), TEXT("generate-level-structure"));
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("status"), TEXT("dry_run_validated"));
		Response->SetStringField(TEXT("structure_type"), StructureType);
		Response->SetObjectField(TEXT("world"), GenerateLevelStructureToolPrivate::BuildWorldPayload(World));
		Response->SetArrayField(TEXT("spawned_actors"), SpawnedActorsArray);
		Response->SetArrayField(TEXT("warnings"), WarningsArray);
		Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		Response->SetObjectField(TEXT("summary"), Summary);
		return FMcpToolResult::StructuredJson(Response);
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), TEXT("Engine Cube mesh '/Engine/BasicShapes/Cube.Cube' could not be loaded"));
	}

	TMap<FString, UMaterialInterface*> MaterialOverrides;
	if (MaterialOverridesObject && (*MaterialOverridesObject).IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MaterialOverridesObject)->Values)
		{
			const FString PartName = Pair.Key;
			const FString MaterialPath = Pair.Value.IsValid() ? Pair.Value->AsString() : TEXT("");
			if (MaterialPath.IsEmpty())
			{
				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("part_name"), PartName);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("material_overrides values must be non-empty material asset paths"), Details);
			}

			FString MaterialError;
			UMaterialInterface* OverrideMaterial = FMcpAssetModifier::LoadAssetByPath<UMaterialInterface>(MaterialPath, MaterialError);
			if (!OverrideMaterial)
			{
				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("part_name"), PartName);
				Details->SetStringField(TEXT("material_path"), MaterialPath);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), MaterialError, Details);
			}

			MaterialOverrides.Add(PartName, OverrideMaterial);
		}
	}

	TSet<FString> UsedMaterialOverrideKeys;
	const FString GeneratedFolderPath = TEXT("Generated");

	const auto SpawnPart = [World, CubeMesh, GeneratedFolderPath, &MaterialOverrides, &UsedMaterialOverrideKeys, &SpawnedActorsArray](
		const FVector& Location,
		const FVector& Scale,
		const FString& PartName,
		FString& OutError) -> bool
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* PartActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!PartActor)
		{
			OutError = FString::Printf(TEXT("Failed to spawn generated part '%s'"), *PartName);
			return false;
		}

		UStaticMeshComponent* StaticMeshComponent = PartActor->GetStaticMeshComponent();
		if (!StaticMeshComponent)
		{
			PartActor->Destroy();
			OutError = FString::Printf(TEXT("Generated part '%s' is missing a StaticMeshComponent"), *PartName);
			return false;
		}

		StaticMeshComponent->SetStaticMesh(CubeMesh);
		PartActor->SetActorLocation(Location);
		PartActor->SetActorScale3D(Scale);
		PartActor->SetActorLabel(PartName);
		PartActor->SetFolderPath(FName(*GeneratedFolderPath));

		if (UMaterialInterface* const* OverrideMaterial = MaterialOverrides.Find(PartName))
		{
			StaticMeshComponent->SetMaterial(0, *OverrideMaterial);
			UsedMaterialOverrideKeys.Add(PartName);
		}

		TSharedPtr<FJsonObject> ActorInfo = MakeShareable(new FJsonObject);
		ActorInfo->SetStringField(TEXT("part_name"), PartName);
		ActorInfo->SetStringField(TEXT("actor_name"), PartActor->GetName());
		ActorInfo->SetStringField(TEXT("actor_label"), PartActor->GetActorNameOrLabel());
		ActorInfo->SetStringField(TEXT("path"), PartActor->GetPathName());
		ActorInfo->SetArrayField(TEXT("location"), GenerateLevelStructureToolPrivate::MakeVectorArray(Location));
		ActorInfo->SetArrayField(TEXT("scale"), GenerateLevelStructureToolPrivate::MakeVectorArray(Scale));
		if (UMaterialInterface* const* OverrideMaterial = MaterialOverrides.Find(PartName))
		{
			ActorInfo->SetStringField(TEXT("material_override"), (*OverrideMaterial)->GetPathName());
		}
		SpawnedActorsArray.Add(MakeShareable(new FJsonValueObject(ActorInfo)));
		return true;
	};

	FString SpawnError;
	if (StructureType == TEXT("box_room"))
	{
		if (!SpawnPart(Origin, FVector(Width / 100.0, Depth / 100.0, 0.1), TEXT("Gen_Floor"), SpawnError)
			|| !SpawnPart(Origin + FVector(0.0, 0.0, Height), FVector(Width / 100.0, Depth / 100.0, 0.1), TEXT("Gen_Ceiling"), SpawnError)
			|| !SpawnPart(Origin + FVector(Width / 2.0, 0.0, Height / 2.0), FVector(0.1, Depth / 100.0, Height / 100.0), TEXT("Gen_Wall_Right"), SpawnError)
			|| !SpawnPart(Origin + FVector(-Width / 2.0, 0.0, Height / 2.0), FVector(0.1, Depth / 100.0, Height / 100.0), TEXT("Gen_Wall_Left"), SpawnError)
			|| !SpawnPart(Origin + FVector(0.0, Depth / 2.0, Height / 2.0), FVector(Width / 100.0, 0.1, Height / 100.0), TEXT("Gen_Wall_Front"), SpawnError)
			|| !SpawnPart(Origin + FVector(0.0, -Depth / 2.0, Height / 2.0), FVector(Width / 100.0, 0.1, Height / 100.0), TEXT("Gen_Wall_Back"), SpawnError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), SpawnError, nullptr,
				GenerateLevelStructureToolPrivate::BuildPartialResult(StructureType, World, SpawnedActorsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray));
		}
	}
	else if (StructureType == TEXT("platform"))
	{
		if (!SpawnPart(Origin, FVector(Width / 100.0, Depth / 100.0, 0.2), TEXT("Gen_Platform"), SpawnError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), SpawnError, nullptr,
				GenerateLevelStructureToolPrivate::BuildPartialResult(StructureType, World, SpawnedActorsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray));
		}
	}
	else if (StructureType == TEXT("wall"))
	{
		if (!SpawnPart(Origin + FVector(0.0, 0.0, Height / 2.0), FVector(Width / 100.0, 0.1, Height / 100.0), TEXT("Gen_Wall"), SpawnError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), SpawnError, nullptr,
				GenerateLevelStructureToolPrivate::BuildPartialResult(StructureType, World, SpawnedActorsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray));
		}
	}
	else if (StructureType == TEXT("corridor"))
	{
		if (!SpawnPart(Origin, FVector(Width / 100.0, Depth / 100.0, 0.1), TEXT("Gen_Corridor_Floor"), SpawnError)
			|| !SpawnPart(Origin + FVector(0.0, Depth / 2.0, Height / 2.0), FVector(Width / 100.0, 0.1, Height / 100.0), TEXT("Gen_Corridor_Wall_L"), SpawnError)
			|| !SpawnPart(Origin + FVector(0.0, -Depth / 2.0, Height / 2.0), FVector(Width / 100.0, 0.1, Height / 100.0), TEXT("Gen_Corridor_Wall_R"), SpawnError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), SpawnError, nullptr,
				GenerateLevelStructureToolPrivate::BuildPartialResult(StructureType, World, SpawnedActorsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray));
		}
	}
	else if (StructureType == TEXT("staircase"))
	{
		const int32 StepCount = FMath::Max(1, FMath::RoundToInt(Height / 30.0));
		const double StepHeight = Height / StepCount;
		const double StepDepth = Depth / StepCount;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const FVector StepLocation = Origin + FVector(0.0, StepIndex * StepDepth, StepIndex * StepHeight + (StepHeight * 0.5));
			const FVector StepScale(Width / 100.0, StepDepth / 100.0, StepHeight / 100.0);
			if (!SpawnPart(StepLocation, StepScale, FString::Printf(TEXT("Gen_Step_%d"), StepIndex), SpawnError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), SpawnError, nullptr,
					GenerateLevelStructureToolPrivate::BuildPartialResult(StructureType, World, SpawnedActorsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray));
			}
		}
	}
	else
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("structure_type"), StructureType);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSUPPORTED_ACTION"), FString::Printf(TEXT("Unknown structure type: '%s'"), *StructureType), Details);
	}

	for (const TPair<FString, UMaterialInterface*>& Pair : MaterialOverrides)
	{
		if (!UsedMaterialOverrideKeys.Contains(Pair.Key))
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("material_overrides entry '%s' did not match any generated part"), *Pair.Key))));
		}
	}

	if (World->PersistentLevel)
	{
		World->PersistentLevel->MarkPackageDirty();
	}
	GenerateLevelStructureToolPrivate::AddModifiedWorldAsset(World, ModifiedAssetsArray);

	bool bSaved = false;
	if (bSave)
	{
		if (!World->PersistentLevel || !FEditorFileUtils::SaveLevel(World->PersistentLevel))
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("map_name"), World->GetMapName());
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_LEVEL_SAVE_FAILED"), TEXT("Failed to save the current editor level"), Details,
				GenerateLevelStructureToolPrivate::BuildPartialResult(StructureType, World, SpawnedActorsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray));
		}
		bSaved = true;
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total_spawned"), SpawnedActorsArray.Num());
	Summary->SetBoolField(TEXT("save_requested"), bSave);
	Summary->SetBoolField(TEXT("saved"), bSaved);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("generate-level-structure"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("structure_type"), StructureType);
	Response->SetObjectField(TEXT("world"), GenerateLevelStructureToolPrivate::BuildWorldPayload(World));
	Response->SetArrayField(TEXT("spawned_actors"), SpawnedActorsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), Summary);

	return FMcpToolResult::StructuredJson(Response);
}
