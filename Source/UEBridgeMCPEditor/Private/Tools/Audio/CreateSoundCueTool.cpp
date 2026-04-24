// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Audio/CreateSoundCueTool.h"

#include "Tools/Audio/AudioToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/SoundCueFactoryNew.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"

FString UCreateSoundCueTool::GetToolDescription() const
{
	return TEXT("Create a SoundCue asset from an optional initial SoundWave and basic volume/pitch settings.");
}

TMap<FString, FMcpSchemaProperty> UCreateSoundCueTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination SoundCue asset path"), true));
	Schema.Add(TEXT("initial_sound_wave_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional initial SoundWave asset path")));
	Schema.Add(TEXT("volume_multiplier"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("SoundCue volume multiplier")));
	Schema.Add(TEXT("pitch_multiplier"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("SoundCue pitch multiplier")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the created SoundCue")));
	return Schema;
}

FMcpToolResult UCreateSoundCueTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString InitialWavePath = GetStringArgOrDefault(Arguments, TEXT("initial_sound_wave_path"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Destination SoundCue already exists"), Details);
	}

	USoundWave* InitialWave = nullptr;
	if (!InitialWavePath.IsEmpty())
	{
		FString LoadError;
		if (!AudioToolUtils::TryLoadSoundWave(InitialWavePath, InitialWave, LoadError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);
	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create SoundCue")));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	USoundCueFactoryNew* Factory = NewObject<USoundCueFactoryNew>();
	if (InitialWave)
	{
		Factory->InitialSoundWaves.Add(InitialWave);
	}

	UObject* CreatedObject = AssetTools.CreateAsset(AssetName, PackagePath, USoundCue::StaticClass(), Factory);
	USoundCue* CreatedCue = Cast<USoundCue>(CreatedObject);
	if (!CreatedCue)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_AUDIO_CREATE_FAILED"), TEXT("Failed to create SoundCue"));
	}

	double NumberValue = 0.0;
	if (Arguments->TryGetNumberField(TEXT("volume_multiplier"), NumberValue))
	{
		CreatedCue->VolumeMultiplier = static_cast<float>(NumberValue);
	}
	if (Arguments->TryGetNumberField(TEXT("pitch_multiplier"), NumberValue))
	{
		CreatedCue->PitchMultiplier = static_cast<float>(NumberValue);
	}

	FString FinalizeError;
	if (CreatedCue->FirstNode && !AudioToolUtils::FinalizeCueEdit(CreatedCue, FinalizeError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_AUDIO_CUE_EDIT_FAILED"), FinalizeError);
	}

	FMcpAssetModifier::MarkPackageDirty(CreatedCue);
	FAssetRegistryModule::AssetCreated(CreatedCue);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (bSave)
	{
		AudioToolUtils::SaveAsset(CreatedCue, Warnings);
	}

	TSharedPtr<FJsonObject> Result = AudioToolUtils::SerializeSoundCue(CreatedCue, true);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("initial_sound_wave_path"), InitialWavePath);
	Result->SetBoolField(TEXT("saved"), bSave && Warnings.Num() == 0);
	Result->SetBoolField(TEXT("needs_save"), CreatedCue->GetOutermost()->IsDirty());
	Result->SetArrayField(TEXT("warnings"), Warnings);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("SoundCue created"));
}
