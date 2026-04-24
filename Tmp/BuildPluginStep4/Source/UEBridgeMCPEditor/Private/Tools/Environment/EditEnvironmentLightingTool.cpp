// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Environment/EditEnvironmentLightingTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
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

namespace
{
	TSharedPtr<FJsonObject> BuildPartialFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> PartialObject = MakeShareable(new FJsonObject);
		PartialObject->SetStringField(TEXT("tool"), ToolName);
		PartialObject->SetArrayField(TEXT("results"), ResultsArray);
		PartialObject->SetArrayField(TEXT("warnings"), WarningsArray);
		PartialObject->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		PartialObject->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		PartialObject->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return PartialObject;
	}

	bool TryReadLinearColorField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FLinearColor& OutColor)
	{
		const TSharedPtr<FJsonObject>* ColorObject = nullptr;
		if (Object->TryGetObjectField(FieldName, ColorObject) && ColorObject && (*ColorObject).IsValid())
		{
			double R = 1.0;
			double G = 1.0;
			double B = 1.0;
			double A = 1.0;
			(*ColorObject)->TryGetNumberField(TEXT("r"), R);
			(*ColorObject)->TryGetNumberField(TEXT("g"), G);
			(*ColorObject)->TryGetNumberField(TEXT("b"), B);
			(*ColorObject)->TryGetNumberField(TEXT("a"), A);
			OutColor = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
		if (Object->TryGetArrayField(FieldName, ColorArray) && ColorArray && ColorArray->Num() >= 3)
		{
			OutColor = FLinearColor(
				static_cast<float>((*ColorArray)[0]->AsNumber()),
				static_cast<float>((*ColorArray)[1]->AsNumber()),
				static_cast<float>((*ColorArray)[2]->AsNumber()),
				ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f);
			return true;
		}

		return false;
	}
}

FString UEditEnvironmentLightingTool::GetToolDescription() const
{
	return TEXT("Transactional environment lighting editing for core editor lighting actors without auto-creating missing actors.");
}

TMap<FString, FMcpSchemaProperty> UEditEnvironmentLightingTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Environment lighting operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Environment lighting action"),
		{
			TEXT("set_directional_light"),
			TEXT("set_skylight"),
			TEXT("set_sky_atmosphere"),
			TEXT("set_exponential_height_fog"),
			TEXT("set_post_process_volume")
		},
		true)));
	OperationSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor name or label"))));
	OperationSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle"))));
	OperationSchema->Properties.Add(TEXT("intensity"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Light intensity"))));
	OperationSchema->Properties.Add(TEXT("light_color"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Linear color {r,g,b,a} or [r,g,b,a]"))));
	OperationSchema->Properties.Add(TEXT("use_temperature"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Use color temperature"))));
	OperationSchema->Properties.Add(TEXT("temperature"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Light temperature in Kelvin"))));
	OperationSchema->Properties.Add(TEXT("source_angle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Directional light source angle"))));
	OperationSchema->Properties.Add(TEXT("real_time_capture"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Sky light real-time capture"))));
	OperationSchema->Properties.Add(TEXT("rayleigh_scattering_scale"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Sky atmosphere Rayleigh scattering scale"))));
	OperationSchema->Properties.Add(TEXT("mie_scattering_scale"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Sky atmosphere Mie scattering scale"))));
	OperationSchema->Properties.Add(TEXT("fog_density"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Fog density"))));
	OperationSchema->Properties.Add(TEXT("fog_height_falloff"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Fog height falloff"))));
	OperationSchema->Properties.Add(TEXT("start_distance"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Fog start distance"))));
	OperationSchema->Properties.Add(TEXT("unbound"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Post-process volume unbound flag"))));
	OperationSchema->Properties.Add(TEXT("blend_weight"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Post-process blend weight"))));
	OperationSchema->Properties.Add(TEXT("exposure_compensation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Post-process exposure compensation"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Environment lighting operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor level after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));

	return Schema;
}

TArray<FString> UEditEnvironmentLightingTool::GetRequiredParams() const
{
	return { TEXT("operations") };
}

FMcpToolResult UEditEnvironmentLightingTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Environment Lighting")));
	}

	UWorld* ActiveWorld = nullptr;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString ActionName;
		(*OperationObject)->TryGetStringField(TEXT("action"), ActionName);
		ResultObject->SetStringField(TEXT("action"), ActionName);

		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ErrorDetails;
		UWorld* ResolvedWorld = nullptr;
		AActor* TargetActor = LevelActorToolUtils::ResolveActorReference(
			*OperationObject,
			WorldType,
			TEXT("actor_name"),
			TEXT("actor_handle"),
			Context,
			ResolvedWorld,
			ErrorCode,
			ErrorMessage,
			ErrorDetails,
			true);
		if (!TargetActor)
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), ErrorMessage);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails, BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			continue;
		}

		if (!ActiveWorld)
		{
			ActiveWorld = ResolvedWorld;
		}
		else if (ResolvedWorld && ActiveWorld->GetPathName() != ResolvedWorld->GetPathName())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_MISMATCH"), TEXT("All environment operations must resolve to the same world"));
		}

		ResultObject->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(TargetActor, Context.SessionId, false, false));

		bool bOperationSuccess = false;
		bool bOperationChanged = false;
		FString OperationError;

		if (ActionName == TEXT("set_directional_light"))
		{
			ADirectionalLight* DirectionalLight = Cast<ADirectionalLight>(TargetActor);
			UDirectionalLightComponent* Component = DirectionalLight ? DirectionalLight->GetComponent() : nullptr;
			if (!Component)
			{
				OperationError = TEXT("Target actor is not a DirectionalLight");
			}
			else
			{
				double Intensity = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("intensity"), Intensity))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->Intensity, static_cast<float>(Intensity));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetIntensity(static_cast<float>(Intensity));
					}
				}

				FLinearColor Color;
				if (TryReadLinearColorField(*OperationObject, TEXT("light_color"), Color))
				{
					bOperationChanged = bOperationChanged || !Component->GetLightColor().Equals(Color);
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetLightColor(Color);
					}
				}

				bool bUseTemperature = false;
				if ((*OperationObject)->TryGetBoolField(TEXT("use_temperature"), bUseTemperature))
				{
					bOperationChanged = bOperationChanged || Component->bUseTemperature != bUseTemperature;
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetUseTemperature(bUseTemperature);
					}
				}

				double Temperature = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("temperature"), Temperature))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->Temperature, static_cast<float>(Temperature));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetTemperature(static_cast<float>(Temperature));
					}
				}

				double SourceAngle = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("source_angle"), SourceAngle))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->LightSourceAngle, static_cast<float>(SourceAngle));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetLightSourceAngle(static_cast<float>(SourceAngle));
					}
				}

				bOperationSuccess = true;
			}
		}
		else if (ActionName == TEXT("set_skylight"))
		{
			ASkyLight* SkyLight = Cast<ASkyLight>(TargetActor);
			USkyLightComponent* Component = SkyLight ? SkyLight->GetLightComponent() : nullptr;
			if (!Component)
			{
				OperationError = TEXT("Target actor is not a SkyLight");
			}
			else
			{
				double Intensity = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("intensity"), Intensity))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->Intensity, static_cast<float>(Intensity));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetIntensity(static_cast<float>(Intensity));
					}
				}

				FLinearColor Color;
				if (TryReadLinearColorField(*OperationObject, TEXT("light_color"), Color))
				{
					bOperationChanged = bOperationChanged || !Component->GetLightColor().Equals(Color);
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetLightColor(Color);
					}
				}

				bool bRealTimeCapture = false;
				if ((*OperationObject)->TryGetBoolField(TEXT("real_time_capture"), bRealTimeCapture))
				{
					bOperationChanged = bOperationChanged || Component->bRealTimeCapture != bRealTimeCapture;
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetRealTimeCapture(bRealTimeCapture);
					}
				}

				bOperationSuccess = true;
			}
		}
		else if (ActionName == TEXT("set_sky_atmosphere"))
		{
			ASkyAtmosphere* Atmosphere = Cast<ASkyAtmosphere>(TargetActor);
			USkyAtmosphereComponent* Component = Atmosphere ? Atmosphere->GetComponent() : nullptr;
			if (!Component)
			{
				OperationError = TEXT("Target actor is not a SkyAtmosphere");
			}
			else
			{
				double RayleighScale = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("rayleigh_scattering_scale"), RayleighScale))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->RayleighScatteringScale, static_cast<float>(RayleighScale));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetRayleighScatteringScale(static_cast<float>(RayleighScale));
					}
				}

				double MieScale = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("mie_scattering_scale"), MieScale))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->MieScatteringScale, static_cast<float>(MieScale));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetMieScatteringScale(static_cast<float>(MieScale));
					}
				}

				bOperationSuccess = true;
			}
		}
		else if (ActionName == TEXT("set_exponential_height_fog"))
		{
			AExponentialHeightFog* FogActor = Cast<AExponentialHeightFog>(TargetActor);
			UExponentialHeightFogComponent* Component = FogActor ? FogActor->GetComponent() : nullptr;
			if (!Component)
			{
				OperationError = TEXT("Target actor is not an ExponentialHeightFog");
			}
			else
			{
				double FogDensity = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("fog_density"), FogDensity))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->FogDensity, static_cast<float>(FogDensity));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetFogDensity(static_cast<float>(FogDensity));
					}
				}

				double FogHeightFalloff = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("fog_height_falloff"), FogHeightFalloff))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->FogHeightFalloff, static_cast<float>(FogHeightFalloff));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetFogHeightFalloff(static_cast<float>(FogHeightFalloff));
					}
				}

				double StartDistance = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("start_distance"), StartDistance))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(Component->StartDistance, static_cast<float>(StartDistance));
					if (!bDryRun)
					{
						Component->Modify();
						Component->SetStartDistance(static_cast<float>(StartDistance));
					}
				}

				bOperationSuccess = true;
			}
		}
		else if (ActionName == TEXT("set_post_process_volume"))
		{
			APostProcessVolume* PostProcessVolume = Cast<APostProcessVolume>(TargetActor);
			if (!PostProcessVolume)
			{
				OperationError = TEXT("Target actor is not a PostProcessVolume");
			}
			else
			{
				bool bUnbound = false;
				if ((*OperationObject)->TryGetBoolField(TEXT("unbound"), bUnbound))
				{
					bOperationChanged = bOperationChanged || (PostProcessVolume->bUnbound != bUnbound);
					if (!bDryRun)
					{
						PostProcessVolume->Modify();
						PostProcessVolume->bUnbound = bUnbound;
					}
				}

				double BlendWeight = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("blend_weight"), BlendWeight))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(PostProcessVolume->BlendWeight, static_cast<float>(BlendWeight));
					if (!bDryRun)
					{
						PostProcessVolume->Modify();
						PostProcessVolume->BlendWeight = static_cast<float>(BlendWeight);
					}
				}

				double ExposureCompensation = 0.0;
				if ((*OperationObject)->TryGetNumberField(TEXT("exposure_compensation"), ExposureCompensation))
				{
					bOperationChanged = bOperationChanged || !FMath::IsNearlyEqual(PostProcessVolume->Settings.AutoExposureBias, static_cast<float>(ExposureCompensation));
					if (!bDryRun)
					{
						PostProcessVolume->Modify();
						PostProcessVolume->Settings.bOverride_AutoExposureBias = true;
						PostProcessVolume->Settings.AutoExposureBias = static_cast<float>(ExposureCompensation);
					}
				}

				if (!bDryRun)
				{
					PostProcessVolume->PostEditChange();
				}
				bOperationSuccess = true;
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!bOperationSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), OperationError);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_OPERATION_FAILED"),
					OperationError,
					nullptr,
					BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	FString ErrorCode;
	FString ErrorMessage;
	if (!LevelActorToolUtils::SaveWorldIfNeeded(ActiveWorld, bSave, WarningsArray, ModifiedAssetsArray, ErrorCode, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("world_name"), ActiveWorld ? ActiveWorld->GetName() : TEXT(""));
	Response->SetStringField(TEXT("world_path"), ActiveWorld ? ActiveWorld->GetPathName() : TEXT(""));
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
