// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FJsonObject;
class FJsonValue;
class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundCue;
class USoundNode;
class USoundWave;
class UWorld;

namespace AudioToolUtils
{
	FString ObjectPath(const UObject* Object);
	FString SoundWaveLoadingBehaviorToString(USoundWave* SoundWave);

	TSharedPtr<FJsonObject> SerializeSoundBase(USoundBase* Sound, bool bIncludeCueNodes, int32 MaxNodeDepth = 8);
	TSharedPtr<FJsonObject> SerializeSoundCue(USoundCue* SoundCue, bool bIncludeNodes, int32 MaxNodeDepth = 8);
	TSharedPtr<FJsonObject> SerializeSoundNode(USoundNode* Node, int32 Depth, int32 MaxDepth);
	TSharedPtr<FJsonObject> SerializeAudioComponent(UAudioComponent* Component);

	bool SaveAsset(UObject* Asset, TArray<TSharedPtr<FJsonValue>>& OutWarnings);

	bool TryLoadSoundBase(const FString& AssetPath, USoundBase*& OutSound, FString& OutError);
	bool TryLoadSoundWave(const FString& AssetPath, USoundWave*& OutWave, FString& OutError);
	bool TryLoadSoundWaves(const TArray<FString>& AssetPaths, TArray<USoundWave*>& OutWaves, FString& OutError);
	bool TryLoadAttenuation(const FString& AssetPath, USoundAttenuation*& OutAttenuation, FString& OutError);

	bool ReadStringArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<FString>& OutValues, FString& OutError);
	bool ReadNumberArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<float>& OutValues, FString& OutError);

	USoundNode* CreateWavePlayerNode(USoundCue* Cue, USoundWave* Wave, bool bLooping);
	bool RebuildCueWithWave(USoundCue* Cue, USoundWave* Wave, bool bLooping, FString& OutError);
	bool RebuildCueWithRandomWaves(USoundCue* Cue, const TArray<USoundWave*>& Waves, const TArray<float>& Weights, bool bRandomizeWithoutReplacement, FString& OutError);
	bool RebuildCueWithMixerWaves(USoundCue* Cue, const TArray<USoundWave*>& Waves, const TArray<float>& Volumes, FString& OutError);
	bool WrapCueWithAttenuation(USoundCue* Cue, USoundAttenuation* Attenuation, bool bOverrideAttenuation, FString& OutError);
	bool FinalizeCueEdit(USoundCue* Cue, FString& OutError);

	UAudioComponent* FindAudioComponent(AActor* Actor, const FString& ComponentName);
	UAudioComponent* CreateAudioComponent(AActor* Actor, const FString& ComponentName);
	bool ApplyAudioComponentSettings(
		UAudioComponent* Component,
		USoundBase* Sound,
		const TSharedPtr<FJsonObject>& Arguments,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		FString& OutError);
}
