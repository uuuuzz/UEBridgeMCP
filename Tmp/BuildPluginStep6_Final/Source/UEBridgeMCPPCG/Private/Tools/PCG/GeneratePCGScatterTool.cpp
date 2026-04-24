// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PCG/GeneratePCGScatterTool.h"

#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGVolume.h"

namespace
{
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

	FString MakeSafeObjectName(const FString& InValue, const FString& DefaultValue)
	{
		const FString Trimmed = InValue.TrimStartAndEnd();
		return ObjectTools::SanitizeObjectName(Trimmed.IsEmpty() ? DefaultValue : Trimmed);
	}

	TSharedPtr<FJsonObject> MakeModifiedAsset(const FString& AssetPath, const FString& ClassName)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("class_name"), ClassName);
		return Object;
	}

	UPCGGraphInterface* ResolveOrCreateGraph(
		const FString& GraphAssetPath,
		const FString& TemplateName,
		const FString& GeneratedGraphAssetPath,
		bool bDryRun,
		bool& bOutCreated,
		FString& OutResolvedGraphPath,
		FString& OutError)
	{
		bOutCreated = false;
		OutResolvedGraphPath = GraphAssetPath;

		if (!GraphAssetPath.IsEmpty())
		{
			return FMcpAssetModifier::LoadAssetByPath<UPCGGraphInterface>(GraphAssetPath, OutError);
		}

		if (TemplateName.IsEmpty())
		{
			OutError = TEXT("Either 'graph_asset_path' or 'template_name' is required");
			return nullptr;
		}

		const FString SafeTemplateName = MakeSafeObjectName(TemplateName, TEXT("BuiltinScatter"));
		OutResolvedGraphPath = GeneratedGraphAssetPath.IsEmpty()
			? FString::Printf(TEXT("/Game/PCG/Generated/%s"), *SafeTemplateName)
			: GeneratedGraphAssetPath;

		if (FMcpAssetModifier::AssetExists(OutResolvedGraphPath))
		{
			return FMcpAssetModifier::LoadAssetByPath<UPCGGraphInterface>(OutResolvedGraphPath, OutError);
		}

		FString ValidateError;
		if (!FMcpAssetModifier::ValidateAssetPath(OutResolvedGraphPath, ValidateError))
		{
			OutError = ValidateError;
			return nullptr;
		}

		bOutCreated = true;
		if (bDryRun)
		{
			return nullptr;
		}

		const FString AssetName = FPackageName::GetShortName(OutResolvedGraphPath);
		UPackage* Package = CreatePackage(*OutResolvedGraphPath);
		if (!Package)
		{
			OutError = TEXT("Failed to create package for generated PCG graph");
			return nullptr;
		}

		UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Graph)
		{
			OutError = TEXT("Failed to create generated PCG graph");
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(Graph);
		FMcpAssetModifier::MarkPackageDirty(Graph);
		return Graph;
	}

	AActor* ResolveOrCreateTargetActor(
		UWorld* World,
		const FString& ActorName,
		const FString& CreateMode,
		const FString& Label,
		const FVector& BoundsMin,
		const FVector& BoundsMax,
		bool bDryRun,
		bool& bOutCreated,
		FString& OutError)
	{
		bOutCreated = false;
		if (!World)
		{
			OutError = TEXT("World is required");
			return nullptr;
		}

		if (!ActorName.IsEmpty())
		{
			if (AActor* ExistingActor = FMcpAssetModifier::FindActorByName(World, ActorName))
			{
				return ExistingActor;
			}
		}

		bOutCreated = true;
		if (bDryRun)
		{
			return nullptr;
		}

		const bool bCreateVolume = CreateMode.Equals(TEXT("volume"), ESearchCase::IgnoreCase);
		UClass* ActorClass = bCreateVolume ? APCGVolume::StaticClass() : AActor::StaticClass();
		const FString SafeLabel = MakeSafeObjectName(Label, bCreateVolume ? TEXT("PCGScatterVolume") : TEXT("PCGScatterActor"));
		const FVector Center = (BoundsMin + BoundsMax) * 0.5f;
		const FVector Extent = (BoundsMax - BoundsMin).GetAbs() * 0.5f;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(World, ActorClass, FName(*SafeLabel));
		SpawnParameters.ObjectFlags |= RF_Transactional;

		AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Center, FRotator::ZeroRotator, SpawnParameters);
		if (!NewActor)
		{
			OutError = TEXT("Failed to spawn a PCG target actor");
			return nullptr;
		}

#if WITH_EDITOR
		NewActor->SetActorLabel(SafeLabel);
#endif

		if (bCreateVolume)
		{
			const FVector SafeScale(
				FMath::Max(Extent.X / 100.0f, 0.1f),
				FMath::Max(Extent.Y / 100.0f, 0.1f),
				FMath::Max(Extent.Z / 100.0f, 0.1f));
			NewActor->SetActorScale3D(SafeScale);
		}

		NewActor->Modify();
		World->Modify();
		return NewActor;
	}

	UPCGComponent* ResolveOrCreatePCGComponent(
		AActor* TargetActor,
		const FString& ComponentName,
		bool bDryRun,
		bool& bOutCreated,
		FString& OutResolvedComponentName)
	{
		bOutCreated = false;
		OutResolvedComponentName = ComponentName;

		if (!TargetActor)
		{
			return nullptr;
		}

		auto MatchesRequestedName = [&](UPCGComponent* Component) -> bool
		{
			return Component && (ComponentName.IsEmpty() || Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase));
		};

		if (APCGVolume* Volume = Cast<APCGVolume>(TargetActor))
		{
			if (MatchesRequestedName(Volume->PCGComponent))
			{
				OutResolvedComponentName = Volume->PCGComponent->GetName();
				return Volume->PCGComponent;
			}
		}

		TArray<UPCGComponent*> ExistingComponents;
		TargetActor->GetComponents(ExistingComponents);
		for (UPCGComponent* ExistingComponent : ExistingComponents)
		{
			if (MatchesRequestedName(ExistingComponent))
			{
				OutResolvedComponentName = ExistingComponent->GetName();
				return ExistingComponent;
			}
		}

		bOutCreated = true;
		OutResolvedComponentName = MakeSafeObjectName(ComponentName, TEXT("PCGScatterComponent"));
		if (bDryRun)
		{
			return nullptr;
		}

		UPCGComponent* NewComponent = NewObject<UPCGComponent>(TargetActor, *OutResolvedComponentName, RF_Transactional);
		if (!NewComponent)
		{
			return nullptr;
		}

		TargetActor->AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
		NewComponent->Modify();
		NewComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		NewComponent->bActivated = true;

		if (APCGVolume* Volume = Cast<APCGVolume>(TargetActor))
		{
			Volume->PCGComponent = NewComponent;
		}

		return NewComponent;
	}
}

FString UGeneratePCGScatterTool::GetToolDescription() const
{
	return TEXT("Create or update a PCG actor/component target, bind an existing graph or generated scaffold graph, and optionally trigger generation.");
}

TMap<FString, FMcpSchemaProperty> UGeneratePCGScatterTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to edit"), { TEXT("editor"), TEXT("pie") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional existing target actor name or label")));
	Schema.Add(TEXT("create_mode"), FMcpSchemaProperty::MakeEnum(TEXT("Target creation mode when actor_name does not resolve"), { TEXT("actor"), TEXT("volume") }));
	Schema.Add(TEXT("label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Requested label when creating a new target actor")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional PCG component name")));
	Schema.Add(TEXT("graph_asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional existing PCG graph asset path")));
	Schema.Add(TEXT("template_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional built-in template scaffold name used when graph_asset_path is omitted")));
	Schema.Add(TEXT("generated_graph_asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional destination path for a generated scaffold graph")));
	Schema.Add(TEXT("bounds_min"), FMcpSchemaProperty::MakeArray(TEXT("Optional bounds min [x,y,z] for created targets"), TEXT("number")));
	Schema.Add(TEXT("bounds_max"), FMcpSchemaProperty::MakeArray(TEXT("Optional bounds max [x,y,z] for created targets"), TEXT("number")));
	Schema.Add(TEXT("generate"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Trigger generation after binding the graph")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save changed assets and world")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate and expand the target plan only")));
	return Schema;
}

FMcpToolResult UGeneratePCGScatterTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	const FString CreateMode = GetStringArgOrDefault(Arguments, TEXT("create_mode"), TEXT("actor")).ToLower();
	const FString Label = GetStringArgOrDefault(Arguments, TEXT("label"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const FString GraphAssetPath = GetStringArgOrDefault(Arguments, TEXT("graph_asset_path"));
	const FString TemplateName = GetStringArgOrDefault(Arguments, TEXT("template_name"));
	const FString GeneratedGraphAssetPath = GetStringArgOrDefault(Arguments, TEXT("generated_graph_asset_path"));
	const bool bGenerate = GetBoolArgOrDefault(Arguments, TEXT("generate"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	if (GraphAssetPath.IsEmpty() && TemplateName.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("Either 'graph_asset_path' or 'template_name' is required"));
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the requested world"));
	}

	FVector BoundsMin = FVector(-500.0f, -500.0f, 0.0f);
	FVector BoundsMax = FVector(500.0f, 500.0f, 500.0f);
	TryReadVectorField(Arguments, TEXT("bounds_min"), BoundsMin);
	TryReadVectorField(Arguments, TEXT("bounds_max"), BoundsMax);

	bool bActorCreated = false;
	FString ActorError;
	AActor* TargetActor = ResolveOrCreateTargetActor(World, ActorName, CreateMode, Label, BoundsMin, BoundsMax, bDryRun, bActorCreated, ActorError);
	if (!TargetActor && !bDryRun)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_OPERATION_FAILED"), ActorError);
	}

	bool bComponentCreated = false;
	FString ResolvedComponentName;
	UPCGComponent* PCGComponent = ResolveOrCreatePCGComponent(TargetActor, ComponentName, bDryRun, bComponentCreated, ResolvedComponentName);
	if (!PCGComponent && !bDryRun)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_OPERATION_FAILED"), TEXT("Failed to resolve or create a PCG component"));
	}

	bool bGraphCreated = false;
	FString ResolvedGraphAssetPath;
	FString GraphError;
	UPCGGraphInterface* GraphInterface = ResolveOrCreateGraph(GraphAssetPath, TemplateName, GeneratedGraphAssetPath, bDryRun, bGraphCreated, ResolvedGraphAssetPath, GraphError);
	if (!GraphError.IsEmpty() && (!bDryRun || !bGraphCreated))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), GraphError);
	}

	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	bool bChanged = bActorCreated || bComponentCreated || bGraphCreated;

	if (!bDryRun && PCGComponent && GraphInterface)
	{
		PCGComponent->Modify();
		PCGComponent->SetGraphLocal(GraphInterface);
		PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		PCGComponent->bActivated = true;
		bChanged = true;
	}

	if (!TemplateName.IsEmpty())
	{
		TSharedPtr<FJsonObject> WarningObject = MakeShareable(new FJsonObject);
		WarningObject->SetStringField(TEXT("message"), TEXT("Built-in PCG templates currently create a scaffold graph asset; author the final scatter graph in the PCG editor if needed."));
		WarningsArray.Add(MakeShareable(new FJsonValueObject(WarningObject)));
	}

	if (!bDryRun && bGenerate && PCGComponent)
	{
		PCGComponent->GenerateLocal(true);
		bChanged = true;
	}

	if (!bDryRun && GraphInterface && bGraphCreated)
	{
		FMcpAssetModifier::MarkPackageDirty(GraphInterface);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueObject(MakeModifiedAsset(ResolvedGraphAssetPath, GraphInterface->GetClass()->GetName()))));
	}

	if (!bDryRun && TargetActor)
	{
		TargetActor->Modify();
		World->Modify();
		FMcpAssetModifier::MarkPackageDirty(World);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueObject(MakeModifiedAsset(World->GetPathName(), World->GetClass()->GetName()))));
	}

	if (!bDryRun && bSave)
	{
		if (GraphInterface && bGraphCreated)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(GraphInterface, false, SaveError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}

		if (bChanged)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(World, false, SaveError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("world_name"), World->GetName());
	Response->SetStringField(TEXT("create_mode"), CreateMode);
	Response->SetStringField(TEXT("target_actor_name"), TargetActor ? TargetActor->GetActorNameOrLabel() : MakeSafeObjectName(Label, CreateMode == TEXT("volume") ? TEXT("PCGScatterVolume") : TEXT("PCGScatterActor")));
	Response->SetStringField(TEXT("target_actor_class"), CreateMode == TEXT("volume") ? APCGVolume::StaticClass()->GetName() : AActor::StaticClass()->GetName());
	Response->SetStringField(TEXT("component_name"), ResolvedComponentName.IsEmpty() ? MakeSafeObjectName(ComponentName, TEXT("PCGScatterComponent")) : ResolvedComponentName);
	Response->SetStringField(TEXT("graph_asset_path"), ResolvedGraphAssetPath);
	Response->SetStringField(TEXT("template_name"), TemplateName);
	Response->SetBoolField(TEXT("actor_created"), bActorCreated);
	Response->SetBoolField(TEXT("component_created"), bComponentCreated);
	Response->SetBoolField(TEXT("graph_created"), bGraphCreated);
	Response->SetBoolField(TEXT("generated"), !bDryRun && bGenerate && PCGComponent != nullptr);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	return FMcpToolResult::StructuredJson(Response);
}
