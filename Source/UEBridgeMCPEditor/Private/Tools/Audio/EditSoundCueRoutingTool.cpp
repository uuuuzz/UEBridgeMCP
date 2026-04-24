// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Audio/EditSoundCueRoutingTool.h"

#include "Tools/Audio/AudioToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "ScopedTransaction.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"

namespace
{
	TSharedPtr<FJsonObject> BuildFailurePayload(
		const TArray<TSharedPtr<FJsonValue>>& Results,
		const TArray<TSharedPtr<FJsonValue>>& Warnings,
		USoundCue* Cue)
	{
		TSharedPtr<FJsonObject> Partial = MakeShareable(new FJsonObject);
		Partial->SetStringField(TEXT("tool"), TEXT("edit-sound-cue-routing"));
		Partial->SetArrayField(TEXT("results"), Results);
		Partial->SetArrayField(TEXT("warnings"), Warnings);
		if (Cue)
		{
			Partial->SetObjectField(TEXT("sound_cue"), AudioToolUtils::SerializeSoundCue(Cue, true));
		}
		return Partial;
	}
}

FString UEditSoundCueRoutingTool::GetToolDescription() const
{
	return TEXT("Batch edit basic SoundCue routing. Supports wave player, random, mixer, attenuation wrapping, and SoundCue volume/pitch settings.");
}

TMap<FString, FMcpSchemaProperty> UEditSoundCueRoutingTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("SoundCue asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("SoundCue routing operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Routing action"),
		{ TEXT("set_wave_player"), TEXT("set_random_waves"), TEXT("set_mixer_waves"), TEXT("wrap_with_attenuation"), TEXT("set_volume_multiplier"), TEXT("set_pitch_multiplier"), TEXT("set_attenuation_settings"), TEXT("set_override_attenuation"), TEXT("clear_routing") },
		true)));
	OperationSchema->Properties.Add(TEXT("sound_wave_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("SoundWave path for set_wave_player"))));
	OperationSchema->Properties.Add(TEXT("sound_wave_paths"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("array"), TEXT("SoundWave paths for random or mixer routing"))));
	OperationSchema->Properties.Add(TEXT("weights"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("array"), TEXT("Random weights matching sound_wave_paths"))));
	OperationSchema->Properties.Add(TEXT("volumes"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("array"), TEXT("Mixer input volumes matching sound_wave_paths"))));
	OperationSchema->Properties.Add(TEXT("looping"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Loop wave player"))));
	OperationSchema->Properties.Add(TEXT("randomize_without_replacement"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Random node without replacement"))));
	OperationSchema->Properties.Add(TEXT("attenuation_settings_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional SoundAttenuation asset path"))));
	OperationSchema->Properties.Add(TEXT("override_attenuation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Enable attenuation overrides"))));
	OperationSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Numeric value for multiplier actions"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Routing operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without mutating the SoundCue")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failed operation")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the SoundCue after successful edits")));
	return Schema;
}

FMcpToolResult UEditSoundCueRoutingTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	USoundCue* Cue = FMcpAssetModifier::LoadAssetByPath<USoundCue>(AssetPath, LoadError);
	if (!Cue)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit SoundCue Routing")));
		Cue->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	bool bAnyFailed = false;
	bool bAnyChanged = false;
	int32 Succeeded = 0;
	int32 Failed = 0;

	for (int32 Index = 0; Index < Operations->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Operation = nullptr;
		if (!(*Operations)[Index].IsValid() || !(*Operations)[Index]->TryGetObject(Operation) || !Operation || !(*Operation).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), Index));
		}

		FString Action;
		(*Operation)->TryGetStringField(TEXT("action"), Action);

		TSharedPtr<FJsonObject> OpResult = MakeShareable(new FJsonObject);
		OpResult->SetNumberField(TEXT("index"), Index);
		OpResult->SetStringField(TEXT("action"), Action);

		bool bSuccess = false;
		bool bChanged = false;
		FString Error;

		if (Action == TEXT("set_wave_player"))
		{
			FString WavePath;
			(*Operation)->TryGetStringField(TEXT("sound_wave_path"), WavePath);
			USoundWave* Wave = nullptr;
			bool bLooping = false;
			(*Operation)->TryGetBoolField(TEXT("looping"), bLooping);
			if (WavePath.IsEmpty())
			{
				Error = TEXT("'sound_wave_path' is required");
			}
			else if (AudioToolUtils::TryLoadSoundWave(WavePath, Wave, Error))
			{
				bSuccess = bDryRun || AudioToolUtils::RebuildCueWithWave(Cue, Wave, bLooping, Error);
				bChanged = true;
				OpResult->SetStringField(TEXT("sound_wave_path"), WavePath);
			}
		}
		else if (Action == TEXT("set_random_waves") || Action == TEXT("set_mixer_waves"))
		{
			TArray<FString> WavePaths;
			TArray<float> Numbers;
			TArray<USoundWave*> Waves;
			const FString NumberField = Action == TEXT("set_random_waves") ? TEXT("weights") : TEXT("volumes");
			if (AudioToolUtils::ReadStringArray(*Operation, TEXT("sound_wave_paths"), WavePaths, Error) &&
				AudioToolUtils::ReadNumberArray(*Operation, NumberField, Numbers, Error) &&
				AudioToolUtils::TryLoadSoundWaves(WavePaths, Waves, Error))
			{
				if (Action == TEXT("set_random_waves"))
				{
					bool bWithoutReplacement = false;
					(*Operation)->TryGetBoolField(TEXT("randomize_without_replacement"), bWithoutReplacement);
					bSuccess = bDryRun || AudioToolUtils::RebuildCueWithRandomWaves(Cue, Waves, Numbers, bWithoutReplacement, Error);
				}
				else
				{
					bSuccess = bDryRun || AudioToolUtils::RebuildCueWithMixerWaves(Cue, Waves, Numbers, Error);
				}
				bChanged = true;
				OpResult->SetNumberField(TEXT("wave_count"), WavePaths.Num());
			}
		}
		else if (Action == TEXT("wrap_with_attenuation"))
		{
			FString AttenuationPath;
			(*Operation)->TryGetStringField(TEXT("attenuation_settings_path"), AttenuationPath);
			USoundAttenuation* Attenuation = nullptr;
			bool bOverride = false;
			(*Operation)->TryGetBoolField(TEXT("override_attenuation"), bOverride);
			if (AudioToolUtils::TryLoadAttenuation(AttenuationPath, Attenuation, Error))
			{
				bSuccess = bDryRun || AudioToolUtils::WrapCueWithAttenuation(Cue, Attenuation, bOverride, Error);
				bChanged = true;
			}
		}
		else if (Action == TEXT("set_volume_multiplier") || Action == TEXT("set_pitch_multiplier"))
		{
			double Value = 0.0;
			if (!(*Operation)->TryGetNumberField(TEXT("value"), Value))
			{
				Error = TEXT("'value' is required");
			}
			else
			{
				if (!bDryRun)
				{
					if (Action == TEXT("set_volume_multiplier"))
					{
						Cue->VolumeMultiplier = static_cast<float>(Value);
					}
					else
					{
						Cue->PitchMultiplier = static_cast<float>(Value);
					}
					FMcpAssetModifier::MarkPackageDirty(Cue);
				}
				bSuccess = true;
				bChanged = true;
				OpResult->SetNumberField(TEXT("value"), Value);
			}
		}
		else if (Action == TEXT("set_attenuation_settings"))
		{
			FString AttenuationPath;
			(*Operation)->TryGetStringField(TEXT("attenuation_settings_path"), AttenuationPath);
			USoundAttenuation* Attenuation = nullptr;
			if (AudioToolUtils::TryLoadAttenuation(AttenuationPath, Attenuation, Error))
			{
				if (!bDryRun)
				{
					Cue->AttenuationSettings = Attenuation;
					FMcpAssetModifier::MarkPackageDirty(Cue);
				}
				bSuccess = true;
				bChanged = true;
			}
		}
		else if (Action == TEXT("set_override_attenuation"))
		{
			bool bOverride = false;
			if (!(*Operation)->TryGetBoolField(TEXT("override_attenuation"), bOverride))
			{
				Error = TEXT("'override_attenuation' is required");
			}
			else
			{
				if (!bDryRun)
				{
					Cue->bOverrideAttenuation = bOverride;
					FMcpAssetModifier::MarkPackageDirty(Cue);
				}
				bSuccess = true;
				bChanged = true;
			}
		}
		else if (Action == TEXT("clear_routing"))
		{
			if (!bDryRun)
			{
				Cue->CreateGraph();
				Cue->ResetGraph();
				FMcpAssetModifier::MarkPackageDirty(Cue);
			}
			bSuccess = true;
			bChanged = true;
		}
		else
		{
			Error = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		OpResult->SetBoolField(TEXT("success"), bSuccess);
		OpResult->SetBoolField(TEXT("changed"), bChanged);
		if (!Error.IsEmpty())
		{
			OpResult->SetStringField(TEXT("error"), Error);
		}
		Results.Add(MakeShareable(new FJsonValueObject(OpResult)));

		if (bSuccess)
		{
			++Succeeded;
			bAnyChanged = bAnyChanged || bChanged;
		}
		else
		{
			++Failed;
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_INVALID_ACTION"),
					Error.IsEmpty() ? TEXT("SoundCue routing operation failed") : Error,
					nullptr,
					BuildFailurePayload(Results, Warnings, Cue));
			}
		}
	}

	if (!bDryRun && bAnyChanged)
	{
		Cue->PostEditChange();
		FMcpAssetModifier::MarkPackageDirty(Cue);
		if (bSave)
		{
			AudioToolUtils::SaveAsset(Cue, Warnings);
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), Operations->Num());
	Summary->SetNumberField(TEXT("succeeded"), Succeeded);
	Summary->SetNumberField(TEXT("failed"), Failed);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), Results);
	Response->SetArrayField(TEXT("warnings"), Warnings);
	Response->SetObjectField(TEXT("summary"), Summary);
	Response->SetObjectField(TEXT("sound_cue"), AudioToolUtils::SerializeSoundCue(Cue, true));
	Response->SetBoolField(TEXT("needs_save"), Cue->GetOutermost()->IsDirty());
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
