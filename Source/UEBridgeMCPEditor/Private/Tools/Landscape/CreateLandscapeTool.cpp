// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Landscape/CreateLandscapeTool.h"

#include "Tools/Landscape/LandscapeToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeInfo.h"
#include "ScopedTransaction.h"

namespace
{
	bool IsSupportedQuadsPerSection(int32 QuadsPerSection)
	{
		static const TSet<int32> SupportedValues = { 7, 15, 31, 63, 127, 255 };
		return SupportedValues.Contains(QuadsPerSection);
	}

	TSharedPtr<FJsonObject> BuildPlannedLandscapeObject(
		const FString& ActorLabel,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale,
		const FIntPoint& ComponentCount,
		int32 SectionsPerComponent,
		int32 QuadsPerSection,
		float LocalHeight)
	{
		const int32 QuadsPerComponent = SectionsPerComponent * QuadsPerSection;
		const int32 SizeX = ComponentCount.X * QuadsPerComponent + 1;
		const int32 SizeY = ComponentCount.Y * QuadsPerComponent + 1;

		TSharedPtr<FJsonObject> Plan = MakeShareable(new FJsonObject);
		Plan->SetStringField(TEXT("actor_label"), ActorLabel);
		Plan->SetObjectField(TEXT("transform"), McpV2ToolUtils::SerializeTransform(FTransform(Rotation, Location, Scale)));
		Plan->SetArrayField(TEXT("component_count"), LandscapeToolUtils::IntPointToArray(ComponentCount));
		Plan->SetNumberField(TEXT("sections_per_component"), SectionsPerComponent);
		Plan->SetNumberField(TEXT("quads_per_section"), QuadsPerSection);
		Plan->SetNumberField(TEXT("component_size_quads"), QuadsPerComponent);
		Plan->SetArrayField(TEXT("resolution"), LandscapeToolUtils::IntPointToArray(FIntPoint(SizeX, SizeY)));
		Plan->SetNumberField(TEXT("initial_local_height"), LocalHeight);
		return Plan;
	}
}

FString UCreateLandscapeTool::GetToolDescription() const
{
	return TEXT("Create a small flat Landscape actor in the editor world for world-production blockouts and validation fixtures.");
}

TMap<FString, FMcpSchemaProperty> UCreateLandscapeTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to edit. v1 supports editor worlds"), { TEXT("editor"), TEXT("pie") }));
	Schema.Add(TEXT("actor_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional label for the new Landscape actor")));
	Schema.Add(TEXT("location"), FMcpSchemaProperty::MakeArray(TEXT("Desired center location [x,y,z]"), TEXT("number")));
	Schema.Add(TEXT("rotation"), FMcpSchemaProperty::MakeArray(TEXT("Actor rotation [pitch,yaw,roll]"), TEXT("number")));
	Schema.Add(TEXT("scale"), FMcpSchemaProperty::MakeArray(TEXT("Actor scale [x,y,z]. Default: [100,100,100]"), TEXT("number")));
	Schema.Add(TEXT("component_count"), FMcpSchemaProperty::MakeArray(TEXT("Landscape component count [x,y]. Default: [1,1]"), TEXT("integer")));
	Schema.Add(TEXT("sections_per_component"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Sections per component. Supported v1 values: 1 or 2. Default: 1")));
	Schema.Add(TEXT("quads_per_section"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Quads per section. Supported values: 7, 15, 31, 63, 127, 255. Default: 7")));
	Schema.Add(TEXT("height"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Initial local landscape height. Default: 0")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate and return the planned landscape without creating it")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited world when possible")));
	return Schema;
}

FMcpToolResult UCreateLandscapeTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	if (!RequestedWorldType.Equals(TEXT("editor"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSUPPORTED_WORLD"), TEXT("create-landscape v1 only supports editor worlds"));
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(TEXT("editor"));
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the editor world"));
	}

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale(100.0, 100.0, 100.0);
	LandscapeToolUtils::TryReadVectorField(Arguments, TEXT("location"), Location);
	FVector RotationVector = FVector::ZeroVector;
	if (LandscapeToolUtils::TryReadVectorField(Arguments, TEXT("rotation"), RotationVector))
	{
		Rotation = FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z);
	}
	LandscapeToolUtils::TryReadVectorField(Arguments, TEXT("scale"), Scale);

	FIntPoint ComponentCount(1, 1);
	LandscapeToolUtils::TryReadIntPointField(Arguments, TEXT("component_count"), ComponentCount);
	ComponentCount.X = FMath::Clamp(ComponentCount.X, 1, 32);
	ComponentCount.Y = FMath::Clamp(ComponentCount.Y, 1, 32);

	const int32 SectionsPerComponent = GetIntArgOrDefault(Arguments, TEXT("sections_per_component"), 1);
	if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("sections_per_component must be 1 or 2"));
	}

	const int32 QuadsPerSection = GetIntArgOrDefault(Arguments, TEXT("quads_per_section"), 7);
	if (!IsSupportedQuadsPerSection(QuadsPerSection))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("quads_per_section must be one of 7, 15, 31, 63, 127, or 255"));
	}

	const float LocalHeight = GetFloatArgOrDefault(Arguments, TEXT("height"), 0.0f);
	const FString ActorLabel = GetStringArgOrDefault(Arguments, TEXT("actor_label"), TEXT("Landscape"));
	const int32 QuadsPerComponent = SectionsPerComponent * QuadsPerSection;
	const int32 SizeX = ComponentCount.X * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCount.Y * QuadsPerComponent + 1;

	TSharedPtr<FJsonObject> Plan = BuildPlannedLandscapeObject(ActorLabel, Location, Rotation, Scale, ComponentCount, SectionsPerComponent, QuadsPerSection, LocalHeight);
	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), GetToolName());
		Response->SetBoolField(TEXT("success"), true);
		Response->SetBoolField(TEXT("dry_run"), true);
		Response->SetObjectField(TEXT("planned_landscape"), Plan);
		Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
		return FMcpToolResult::StructuredSuccess(Response, TEXT("Landscape creation plan ready"));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("Create Landscape")));

	const FVector CenteringOffset = FTransform(Rotation, FVector::ZeroVector, Scale).TransformVector(
		FVector(-ComponentCount.X * QuadsPerComponent / 2.0, -ComponentCount.Y * QuadsPerComponent / 2.0, 0.0));

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location + CenteringOffset, Rotation);
	if (!Landscape)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CREATE_FAILED"), TEXT("Failed to spawn Landscape actor"));
	}

	Landscape->Modify();
	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->SetActorLabel(ActorLabel, true);
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), static_cast<uint32>(2));

	TArray<uint16> HeightData;
	HeightData.Init(LandscapeDataAccess::GetTexHeight(LocalHeight), SizeX * SizeY);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(
		FGuid::NewGuid(),
		0,
		0,
		SizeX - 1,
		SizeY - 1,
		SectionsPerComponent,
		QuadsPerSection,
		HeightDataPerLayers,
		TEXT(""),
		MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
	}

	Landscape->RegisterAllComponents();
	Landscape->PostEditChange();
	FMcpAssetModifier::MarkPackageDirty(World);

	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	LevelActorToolUtils::AppendWorldModifiedAsset(World, ModifiedAssetsArray);

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	FString SaveErrorCode;
	FString SaveErrorMessage;
	if (!LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, WarningsArray, ModifiedAssetsArray, SaveErrorCode, SaveErrorMessage))
	{
		Transaction.Cancel();
		return FMcpToolResult::StructuredError(SaveErrorCode, SaveErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), false);
	Response->SetObjectField(TEXT("planned_landscape"), Plan);
	Response->SetObjectField(TEXT("landscape"), LandscapeToolUtils::BuildLandscapeSummary(Landscape, Context.SessionId, true, 8, { FIntPoint(0, 0) }));
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	return FMcpToolResult::StructuredSuccess(Response, TEXT("Landscape created"));
}
