// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Environment/QueryEnvironmentSummaryTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	TSharedPtr<FJsonObject> LinearColorToJson(const FLinearColor& Color)
	{
		TSharedPtr<FJsonObject> ColorObject = MakeShareable(new FJsonObject);
		ColorObject->SetNumberField(TEXT("r"), Color.R);
		ColorObject->SetNumberField(TEXT("g"), Color.G);
		ColorObject->SetNumberField(TEXT("b"), Color.B);
		ColorObject->SetNumberField(TEXT("a"), Color.A);
		return ColorObject;
	}

	TSharedPtr<FJsonObject> BuildDirectionalLightSummary(ADirectionalLight* Actor, const FString& SessionId)
	{
		TSharedPtr<FJsonObject> Result = McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, false, false);
		UDirectionalLightComponent* Component = Actor ? Actor->GetComponent() : nullptr;
		if (Result.IsValid() && Component)
		{
			Result->SetNumberField(TEXT("intensity"), Component->Intensity);
			Result->SetObjectField(TEXT("light_color"), LinearColorToJson(Component->GetLightColor()));
			Result->SetBoolField(TEXT("use_temperature"), Component->bUseTemperature);
			Result->SetNumberField(TEXT("temperature"), Component->Temperature);
			Result->SetNumberField(TEXT("source_angle"), Component->LightSourceAngle);
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildSkyLightSummary(ASkyLight* Actor, const FString& SessionId)
	{
		TSharedPtr<FJsonObject> Result = McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, false, false);
		USkyLightComponent* Component = Actor ? Actor->GetLightComponent() : nullptr;
		if (Result.IsValid() && Component)
		{
			Result->SetNumberField(TEXT("intensity"), Component->Intensity);
			Result->SetObjectField(TEXT("light_color"), LinearColorToJson(Component->GetLightColor()));
			Result->SetBoolField(TEXT("real_time_capture"), Component->bRealTimeCapture);
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildSkyAtmosphereSummary(ASkyAtmosphere* Actor, const FString& SessionId)
	{
		TSharedPtr<FJsonObject> Result = McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, false, false);
		USkyAtmosphereComponent* Component = Actor ? Actor->GetComponent() : nullptr;
		if (Result.IsValid() && Component)
		{
			Result->SetNumberField(TEXT("rayleigh_scattering_scale"), Component->RayleighScatteringScale);
			Result->SetNumberField(TEXT("mie_scattering_scale"), Component->MieScatteringScale);
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildFogSummary(AExponentialHeightFog* Actor, const FString& SessionId)
	{
		TSharedPtr<FJsonObject> Result = McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, false, false);
		UExponentialHeightFogComponent* Component = Actor ? Actor->GetComponent() : nullptr;
		if (Result.IsValid() && Component)
		{
			Result->SetNumberField(TEXT("fog_density"), Component->FogDensity);
			Result->SetNumberField(TEXT("fog_height_falloff"), Component->FogHeightFalloff);
			Result->SetNumberField(TEXT("start_distance"), Component->StartDistance);
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildPostProcessSummary(APostProcessVolume* Actor, const FString& SessionId)
	{
		TSharedPtr<FJsonObject> Result = McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, false, false);
		if (Result.IsValid() && Actor)
		{
			Result->SetBoolField(TEXT("unbound"), Actor->bUnbound);
			Result->SetNumberField(TEXT("blend_weight"), Actor->BlendWeight);
			Result->SetNumberField(TEXT("exposure_compensation"), Actor->Settings.AutoExposureBias);
		}
		return Result;
	}
}

FString UQueryEnvironmentSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize editor environment lighting actors including directional lights, sky lights, sky atmosphere, exponential height fog, and post process volumes.");
}

TMap<FString, FMcpSchemaProperty> UQueryEnvironmentSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	return Schema;
}

TArray<FString> UQueryEnvironmentSummaryTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UQueryEnvironmentSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	TArray<TSharedPtr<FJsonValue>> DirectionalLightsArray;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		DirectionalLightsArray.Add(MakeShareable(new FJsonValueObject(BuildDirectionalLightSummary(*It, Context.SessionId))));
	}

	TArray<TSharedPtr<FJsonValue>> SkyLightsArray;
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		SkyLightsArray.Add(MakeShareable(new FJsonValueObject(BuildSkyLightSummary(*It, Context.SessionId))));
	}

	TArray<TSharedPtr<FJsonValue>> SkyAtmospheresArray;
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		SkyAtmospheresArray.Add(MakeShareable(new FJsonValueObject(BuildSkyAtmosphereSummary(*It, Context.SessionId))));
	}

	TArray<TSharedPtr<FJsonValue>> FogsArray;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		FogsArray.Add(MakeShareable(new FJsonValueObject(BuildFogSummary(*It, Context.SessionId))));
	}

	TArray<TSharedPtr<FJsonValue>> PostProcessVolumesArray;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		PostProcessVolumesArray.Add(MakeShareable(new FJsonValueObject(BuildPostProcessSummary(*It, Context.SessionId))));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("world_path"), World->GetPathName());
	Result->SetArrayField(TEXT("directional_lights"), DirectionalLightsArray);
	Result->SetArrayField(TEXT("sky_lights"), SkyLightsArray);
	Result->SetArrayField(TEXT("sky_atmospheres"), SkyAtmospheresArray);
	Result->SetArrayField(TEXT("exponential_height_fogs"), FogsArray);
	Result->SetArrayField(TEXT("post_process_volumes"), PostProcessVolumesArray);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Environment summary ready"));
}
