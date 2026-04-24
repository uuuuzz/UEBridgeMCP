// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/GenerateLevelPatternTool.h"

#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "ScopedTransaction.h"

namespace
{
	struct FLevelPatternActorSpec
	{
		FString Name;
		FString Role;
		FVector Location = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
	};

	TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
	{
		return MakeShareable(new FJsonValueString(Value));
	}

	TArray<TSharedPtr<FJsonValue>> MakeVectorArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	FVector ReadVectorField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const FVector& DefaultValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() < 3)
		{
			return DefaultValue;
		}
		return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
	}

	TSharedPtr<FJsonObject> BuildWorldPayload(UWorld* World)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("type"), TEXT("editor"));
		Object->SetBoolField(TEXT("available"), World != nullptr);
		if (World)
		{
			Object->SetStringField(TEXT("map_name"), World->GetMapName());
			Object->SetStringField(TEXT("path"), World->GetPathName());
			if (World->PersistentLevel && World->PersistentLevel->GetPackage())
			{
				Object->SetStringField(TEXT("package_name"), World->PersistentLevel->GetPackage()->GetName());
			}
		}
		return Object;
	}

	TArray<FLevelPatternActorSpec> BuildSpecs(const FString& Pattern, const FVector& Origin)
	{
		TArray<FLevelPatternActorSpec> Specs;
		if (Pattern == TEXT("test_anchor_pair"))
		{
			Specs.Add({ TEXT("StartAnchor"), TEXT("anchor"), Origin, FVector(0.5, 0.5, 0.5) });
			Specs.Add({ TEXT("EndAnchor"), TEXT("anchor"), Origin + FVector(400.0, 0.0, 0.0), FVector(0.5, 0.5, 0.5) });
		}
		else if (Pattern == TEXT("interaction_test_lane"))
		{
			Specs.Add({ TEXT("LaneFloor"), TEXT("floor"), Origin + FVector(450.0, 0.0, -5.0), FVector(9.0, 1.5, 0.1) });
			Specs.Add({ TEXT("EntryMarker"), TEXT("anchor"), Origin, FVector(0.35, 0.35, 0.35) });
			Specs.Add({ TEXT("InteractionTarget"), TEXT("target"), Origin + FVector(450.0, 0.0, 50.0), FVector(1.0, 1.0, 1.0) });
			Specs.Add({ TEXT("ExitMarker"), TEXT("anchor"), Origin + FVector(900.0, 0.0, 0.0), FVector(0.35, 0.35, 0.35) });
		}
		else if (Pattern == TEXT("lighting_blockout_minimal"))
		{
			Specs.Add({ TEXT("StageFloor"), TEXT("floor"), Origin + FVector(0.0, 0.0, -5.0), FVector(5.0, 5.0, 0.1) });
			Specs.Add({ TEXT("KeyLightProxy"), TEXT("light_proxy"), Origin + FVector(-250.0, -250.0, 300.0), FVector(0.35, 0.35, 0.35) });
			Specs.Add({ TEXT("FillLightProxy"), TEXT("light_proxy"), Origin + FVector(250.0, 200.0, 220.0), FVector(0.35, 0.35, 0.35) });
			Specs.Add({ TEXT("FocusTarget"), TEXT("target"), Origin + FVector(0.0, 0.0, 75.0), FVector(0.75, 0.75, 0.75) });
		}
		return Specs;
	}

	TSharedPtr<FJsonObject> SerializeSpec(const FLevelPatternActorSpec& Spec, const FString& ActorLabel)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Spec.Name);
		Object->SetStringField(TEXT("role"), Spec.Role);
		Object->SetStringField(TEXT("actor_label"), ActorLabel);
		Object->SetStringField(TEXT("actor_class"), TEXT("/Script/Engine.StaticMeshActor"));
		Object->SetArrayField(TEXT("location"), MakeVectorArray(Spec.Location));
		Object->SetArrayField(TEXT("scale"), MakeVectorArray(Spec.Scale));
		return Object;
	}

	void AddModifiedWorldAsset(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		if (World && World->PersistentLevel && World->PersistentLevel->GetPackage())
		{
			OutModifiedAssets.Add(MakeStringValue(World->PersistentLevel->GetPackage()->GetName()));
		}
	}
}

FString UGenerateLevelPatternTool::GetToolDescription() const
{
	return TEXT("Generate curated, engine-only editor-world level patterns for reusable validation and blockout scaffolds.");
}

TMap<FString, FMcpSchemaProperty> UGenerateLevelPatternTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("pattern"), FMcpSchemaProperty::MakeEnum(
		TEXT("Curated level pattern"),
		{
			TEXT("test_anchor_pair"),
			TEXT("interaction_test_lane"),
			TEXT("lighting_blockout_minimal")
		},
		true));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world. v1 supports editor only."), { TEXT("editor") }));
	Schema.Add(TEXT("origin"), FMcpSchemaProperty::MakeArray(TEXT("Origin [X,Y,Z]. Default: [0,0,0]."), TEXT("number")));
	Schema.Add(TEXT("prefix"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor label prefix. Defaults to UEBMCP_<pattern>.")));
	Schema.Add(TEXT("folder_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("World Outliner folder. Default: Generated/MacroUtility.")));
	Schema.Add(TEXT("tags"), FMcpSchemaProperty::MakeArray(TEXT("Additional actor tags to add to generated actors."), TEXT("string")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate and return planned actors without spawning.")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after generation.")));
	return Schema;
}

FMcpToolResult UGenerateLevelPatternTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Pattern = GetStringArgOrDefault(Arguments, TEXT("pattern"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const FString FolderPath = GetStringArgOrDefault(Arguments, TEXT("folder_path"), TEXT("Generated/MacroUtility"));
	const FString Prefix = GetStringArgOrDefault(Arguments, TEXT("prefix"), FString::Printf(TEXT("UEBMCP_%s"), *Pattern));
	const FVector Origin = ReadVectorField(Arguments, TEXT("origin"), FVector::ZeroVector);

	if (!WorldType.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSUPPORTED_COMBINATION"), TEXT("generate-level-pattern v1 only supports world='editor'"));
	}
	if (Pattern != TEXT("test_anchor_pair")
		&& Pattern != TEXT("interaction_test_lane")
		&& Pattern != TEXT("lighting_blockout_minimal"))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), FString::Printf(TEXT("Unsupported level pattern '%s'"), *Pattern));
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

	TArray<FName> Tags;
	Tags.Add(TEXT("UEBridgeMCPGenerated"));
	Tags.Add(TEXT("UEBMCP_LevelPattern"));
	Tags.Add(FName(*Pattern));
	const TArray<TSharedPtr<FJsonValue>>* TagValues = nullptr;
	if (Arguments->TryGetArrayField(TEXT("tags"), TagValues) && TagValues)
	{
		for (const TSharedPtr<FJsonValue>& TagValue : *TagValues)
		{
			const FString Tag = TagValue.IsValid() ? TagValue->AsString() : FString();
			if (!Tag.IsEmpty())
			{
				Tags.AddUnique(FName(*Tag));
			}
		}
	}

	const TArray<FLevelPatternActorSpec> Specs = BuildSpecs(Pattern, Origin);
	TArray<TSharedPtr<FJsonValue>> PlannedActors;
	for (const FLevelPatternActorSpec& Spec : Specs)
	{
		PlannedActors.Add(MakeShareable(new FJsonValueObject(SerializeSpec(Spec, FString::Printf(TEXT("%s_%s"), *Prefix, *Spec.Name)))));
	}

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	TArray<TSharedPtr<FJsonValue>> SpawnedActorsArray;

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetNumberField(TEXT("planned_actor_count"), PlannedActors.Num());
		Summary->SetBoolField(TEXT("dry_run"), true);
		Summary->SetBoolField(TEXT("save_requested"), bSave);

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), GetToolName());
		Response->SetBoolField(TEXT("success"), true);
		Response->SetBoolField(TEXT("dry_run"), true);
		Response->SetStringField(TEXT("pattern"), Pattern);
		Response->SetStringField(TEXT("prefix"), Prefix);
		Response->SetStringField(TEXT("folder_path"), FolderPath);
		Response->SetObjectField(TEXT("world"), BuildWorldPayload(World));
		Response->SetArrayField(TEXT("planned_actors"), PlannedActors);
		Response->SetArrayField(TEXT("spawned_actors"), SpawnedActorsArray);
		Response->SetArrayField(TEXT("warnings"), WarningsArray);
		Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		Response->SetObjectField(TEXT("summary"), Summary);
		return FMcpToolResult::StructuredSuccess(Response, TEXT("Level pattern plan ready"));
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), TEXT("Engine Cube mesh '/Engine/BasicShapes/Cube.Cube' could not be loaded"));
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Generate Level Pattern")));
	for (const FLevelPatternActorSpec& Spec : Specs)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!Actor)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_SPAWN_FAILED"), FString::Printf(TEXT("Failed to spawn actor for pattern part '%s'"), *Spec.Name));
		}

		Actor->Modify();
		Actor->SetActorLocation(Spec.Location);
		Actor->SetActorScale3D(Spec.Scale);
		Actor->SetActorLabel(FString::Printf(TEXT("%s_%s"), *Prefix, *Spec.Name));
		Actor->SetFolderPath(FName(*FolderPath));
		Actor->Tags = Tags;

		if (UStaticMeshComponent* StaticMeshComponent = Actor->GetStaticMeshComponent())
		{
			StaticMeshComponent->SetStaticMesh(CubeMesh);
		}

		TSharedPtr<FJsonObject> ActorObject = SerializeSpec(Spec, Actor->GetActorNameOrLabel());
		ActorObject->SetStringField(TEXT("actor_name"), Actor->GetName());
		ActorObject->SetStringField(TEXT("path"), Actor->GetPathName());
		SpawnedActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObject)));
	}

	if (World->PersistentLevel)
	{
		World->PersistentLevel->MarkPackageDirty();
	}
	AddModifiedWorldAsset(World, ModifiedAssetsArray);

	bool bSaved = false;
	if (bSave)
	{
		if (!World->PersistentLevel || !FEditorFileUtils::SaveLevel(World->PersistentLevel))
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("map_name"), World->GetMapName());
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_LEVEL_SAVE_FAILED"), TEXT("Failed to save the current editor level"), Details);
		}
		bSaved = true;
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("planned_actor_count"), PlannedActors.Num());
	Summary->SetNumberField(TEXT("spawned_actor_count"), SpawnedActorsArray.Num());
	Summary->SetBoolField(TEXT("dry_run"), false);
	Summary->SetBoolField(TEXT("save_requested"), bSave);
	Summary->SetBoolField(TEXT("saved"), bSaved);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), false);
	Response->SetStringField(TEXT("pattern"), Pattern);
	Response->SetStringField(TEXT("prefix"), Prefix);
	Response->SetStringField(TEXT("folder_path"), FolderPath);
	Response->SetObjectField(TEXT("world"), BuildWorldPayload(World));
	Response->SetArrayField(TEXT("planned_actors"), PlannedActors);
	Response->SetArrayField(TEXT("spawned_actors"), SpawnedActorsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredSuccess(Response, TEXT("Level pattern generated"));
}
