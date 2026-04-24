// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Audio/AudioToolUtils.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveLoadingBehavior.h"
#include "UObject/UnrealType.h"

namespace
{
	void EnsureAudioEditorLoaded()
	{
		if (FModuleManager::Get().ModuleExists(TEXT("AudioEditor")) && !FModuleManager::Get().IsModuleLoaded(TEXT("AudioEditor")))
		{
			FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AudioEditor"));
		}
	}

	TArray<TSharedPtr<FJsonValue>> FloatArrayToJson(const TArray<float>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (float Value : Values)
		{
			JsonValues.Add(MakeShareable(new FJsonValueNumber(Value)));
		}
		return JsonValues;
	}

	TArray<TSharedPtr<FJsonValue>> StringSetToJson(const TSet<TObjectPtr<USoundConcurrency>>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (USoundConcurrency* Value : Values)
		{
			if (Value)
			{
				JsonValues.Add(MakeShareable(new FJsonValueString(AudioToolUtils::ObjectPath(Value))));
			}
		}
		return JsonValues;
	}

	double ReadNumericProperty(const UObject* Object, const FName PropertyName, double DefaultValue = 0.0)
	{
		if (!Object)
		{
			return DefaultValue;
		}

		const FNumericProperty* NumericProperty = FindFProperty<FNumericProperty>(Object->GetClass(), PropertyName);
		if (!NumericProperty)
		{
			return DefaultValue;
		}

		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		if (NumericProperty->IsFloatingPoint())
		{
			return NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
		}
		if (NumericProperty->IsInteger())
		{
			return static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		}
		return DefaultValue;
	}

	void AddSoundBaseFields(TSharedPtr<FJsonObject> Object, USoundBase* Sound)
	{
		if (!Object.IsValid() || !Sound)
		{
			return;
		}

		Object->SetStringField(TEXT("asset_path"), AudioToolUtils::ObjectPath(Sound));
		Object->SetStringField(TEXT("asset_class"), Sound->GetClass()->GetName());
		Object->SetStringField(TEXT("name"), Sound->GetName());
		Object->SetNumberField(TEXT("duration"), Sound->GetDuration());
		Object->SetNumberField(TEXT("max_distance"), Sound->GetMaxDistance());
		Object->SetNumberField(TEXT("volume_multiplier"), Sound->GetVolumeMultiplier());
		Object->SetNumberField(TEXT("pitch_multiplier"), Sound->GetPitchMultiplier());
		Object->SetNumberField(TEXT("priority"), Sound->GetPriority());
		Object->SetStringField(TEXT("sound_class"), AudioToolUtils::ObjectPath(Sound->SoundClassObject));
		Object->SetStringField(TEXT("attenuation_settings"), AudioToolUtils::ObjectPath(Sound->AttenuationSettings));
		Object->SetArrayField(TEXT("concurrency"), StringSetToJson(Sound->ConcurrencySet));
	}

	void ResetCueGraph(USoundCue* Cue)
	{
		if (!Cue)
		{
			return;
		}

		EnsureAudioEditorLoaded();
		Cue->CreateGraph();
		Cue->ResetGraph();
	}

	void SetNodeChildren(USoundNode* Node, const TArray<USoundNode*>& Children)
	{
		if (!Node)
		{
			return;
		}

		while (Node->ChildNodes.Num() < Children.Num())
		{
			Node->InsertChildNode(Node->ChildNodes.Num());
		}
		while (Node->ChildNodes.Num() > Children.Num())
		{
			Node->RemoveChildNode(Node->ChildNodes.Num() - 1);
		}
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			Node->ChildNodes[Index] = Children[Index];
		}
	}

	template<typename T>
	T* ConstructCueNode(USoundCue* Cue)
	{
		if (!Cue)
		{
			return nullptr;
		}

		EnsureAudioEditorLoaded();
		Cue->CreateGraph();
		return Cue->ConstructSoundNode<T>(T::StaticClass(), false);
	}
}

namespace AudioToolUtils
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString SoundWaveLoadingBehaviorToString(USoundWave* SoundWave)
	{
		if (!SoundWave)
		{
			return TEXT("unknown");
		}

		const UEnum* Enum = StaticEnum<ESoundWaveLoadingBehavior>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(SoundWave->GetLoadingBehavior())) : FString::FromInt(static_cast<int32>(SoundWave->GetLoadingBehavior()));
	}

	TSharedPtr<FJsonObject> SerializeSoundBase(USoundBase* Sound, bool bIncludeCueNodes, int32 MaxNodeDepth)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		if (!Sound)
		{
			Result->SetBoolField(TEXT("valid"), false);
			return Result;
		}

		Result->SetBoolField(TEXT("valid"), true);
		AddSoundBaseFields(Result, Sound);

		if (USoundWave* Wave = Cast<USoundWave>(Sound))
		{
			Result->SetStringField(TEXT("audio_asset_type"), TEXT("SoundWave"));
			Result->SetNumberField(TEXT("sample_rate"), ReadNumericProperty(Wave, TEXT("SampleRate")));
			Result->SetNumberField(TEXT("num_channels"), ReadNumericProperty(Wave, TEXT("NumChannels")));
			Result->SetBoolField(TEXT("looping"), Wave->bLooping != 0);
			Result->SetBoolField(TEXT("streaming"), Wave->bStreaming != 0);
			Result->SetStringField(TEXT("loading_behavior"), SoundWaveLoadingBehaviorToString(Wave));
			Result->SetStringField(TEXT("sound_group"), StaticEnum<ESoundGroup>() ? StaticEnum<ESoundGroup>()->GetNameStringByValue(Wave->SoundGroup) : FString::FromInt(Wave->SoundGroup));
		}
		else if (USoundCue* Cue = Cast<USoundCue>(Sound))
		{
			Result->SetStringField(TEXT("audio_asset_type"), TEXT("SoundCue"));
			Result->SetObjectField(TEXT("sound_cue"), SerializeSoundCue(Cue, bIncludeCueNodes, MaxNodeDepth));
		}
		else
		{
			Result->SetStringField(TEXT("audio_asset_type"), TEXT("SoundBase"));
		}

		return Result;
	}

	TSharedPtr<FJsonObject> SerializeSoundCue(USoundCue* SoundCue, bool bIncludeNodes, int32 MaxNodeDepth)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		if (!SoundCue)
		{
			Result->SetBoolField(TEXT("valid"), false);
			return Result;
		}

		Result->SetBoolField(TEXT("valid"), true);
		Result->SetStringField(TEXT("asset_path"), ObjectPath(SoundCue));
		Result->SetNumberField(TEXT("volume_multiplier"), SoundCue->VolumeMultiplier);
		Result->SetNumberField(TEXT("pitch_multiplier"), SoundCue->PitchMultiplier);
		Result->SetBoolField(TEXT("override_attenuation"), SoundCue->bOverrideAttenuation != 0);
		Result->SetStringField(TEXT("root_node_class"), SoundCue->FirstNode ? SoundCue->FirstNode->GetClass()->GetName() : FString());
		Result->SetBoolField(TEXT("has_root_node"), SoundCue->FirstNode != nullptr);
		if (bIncludeNodes)
		{
			Result->SetObjectField(TEXT("root_node"), SerializeSoundNode(SoundCue->FirstNode, 0, MaxNodeDepth));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> SerializeSoundNode(USoundNode* Node, int32 Depth, int32 MaxDepth)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		if (!Node)
		{
			Result->SetBoolField(TEXT("valid"), false);
			return Result;
		}

		Result->SetBoolField(TEXT("valid"), true);
		Result->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		Result->SetNumberField(TEXT("depth"), Depth);
		Result->SetNumberField(TEXT("child_count"), Node->ChildNodes.Num());

		if (USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(Node))
		{
			Result->SetStringField(TEXT("node_type"), TEXT("wave_player"));
			Result->SetStringField(TEXT("sound_wave"), ObjectPath(WavePlayer->GetSoundWave()));
			Result->SetBoolField(TEXT("looping"), WavePlayer->bLooping != 0);
		}
		else if (USoundNodeRandom* Random = Cast<USoundNodeRandom>(Node))
		{
			Result->SetStringField(TEXT("node_type"), TEXT("random"));
			Result->SetArrayField(TEXT("weights"), FloatArrayToJson(Random->Weights));
			Result->SetBoolField(TEXT("randomize_without_replacement"), Random->bRandomizeWithoutReplacement != 0);
		}
		else if (USoundNodeMixer* Mixer = Cast<USoundNodeMixer>(Node))
		{
			Result->SetStringField(TEXT("node_type"), TEXT("mixer"));
			Result->SetArrayField(TEXT("input_volumes"), FloatArrayToJson(Mixer->InputVolume));
		}
		else if (USoundNodeAttenuation* Attenuation = Cast<USoundNodeAttenuation>(Node))
		{
			Result->SetStringField(TEXT("node_type"), TEXT("attenuation"));
			Result->SetBoolField(TEXT("override_attenuation"), Attenuation->bOverrideAttenuation != 0);
			Result->SetStringField(TEXT("attenuation_settings"), ObjectPath(Attenuation->AttenuationSettings));
		}
		else
		{
			Result->SetStringField(TEXT("node_type"), TEXT("generic"));
		}

		if (Depth < MaxDepth)
		{
			TArray<TSharedPtr<FJsonValue>> Children;
			for (USoundNode* Child : Node->ChildNodes)
			{
				Children.Add(MakeShareable(new FJsonValueObject(SerializeSoundNode(Child, Depth + 1, MaxDepth))));
			}
			Result->SetArrayField(TEXT("children"), Children);
		}
		else if (Node->ChildNodes.Num() > 0)
		{
			Result->SetBoolField(TEXT("children_truncated"), true);
		}

		return Result;
	}

	TSharedPtr<FJsonObject> SerializeAudioComponent(UAudioComponent* Component)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		if (!Component)
		{
			Result->SetBoolField(TEXT("valid"), false);
			return Result;
		}

		Result->SetBoolField(TEXT("valid"), true);
		Result->SetStringField(TEXT("component_name"), Component->GetName());
		Result->SetStringField(TEXT("sound_path"), ObjectPath(Component->Sound));
		Result->SetNumberField(TEXT("volume_multiplier"), Component->VolumeMultiplier);
		Result->SetNumberField(TEXT("pitch_multiplier"), Component->PitchMultiplier);
		Result->SetBoolField(TEXT("auto_activate"), Component->bAutoActivate);
		Result->SetBoolField(TEXT("active"), Component->IsActive());
		Result->SetBoolField(TEXT("playing"), Component->IsPlaying());
		Result->SetBoolField(TEXT("override_attenuation"), Component->bOverrideAttenuation != 0);
		Result->SetStringField(TEXT("attenuation_settings"), ObjectPath(Component->AttenuationSettings));
		Result->SetArrayField(TEXT("concurrency"), StringSetToJson(Component->ConcurrencySet));
		return Result;
	}

	bool SaveAsset(UObject* Asset, TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Asset, false, SaveError))
		{
			OutWarnings.Add(MakeShareable(new FJsonValueString(SaveError)));
			return false;
		}
		return true;
	}

	bool TryLoadSoundBase(const FString& AssetPath, USoundBase*& OutSound, FString& OutError)
	{
		OutSound = FMcpAssetModifier::LoadAssetByPath<USoundBase>(AssetPath, OutError);
		return OutSound != nullptr;
	}

	bool TryLoadSoundWave(const FString& AssetPath, USoundWave*& OutWave, FString& OutError)
	{
		OutWave = FMcpAssetModifier::LoadAssetByPath<USoundWave>(AssetPath, OutError);
		return OutWave != nullptr;
	}

	bool TryLoadSoundWaves(const TArray<FString>& AssetPaths, TArray<USoundWave*>& OutWaves, FString& OutError)
	{
		OutWaves.Reset();
		for (const FString& AssetPath : AssetPaths)
		{
			USoundWave* Wave = nullptr;
			if (!TryLoadSoundWave(AssetPath, Wave, OutError))
			{
				return false;
			}
			OutWaves.Add(Wave);
		}
		return true;
	}

	bool TryLoadAttenuation(const FString& AssetPath, USoundAttenuation*& OutAttenuation, FString& OutError)
	{
		OutAttenuation = nullptr;
		if (AssetPath.IsEmpty())
		{
			return true;
		}

		OutAttenuation = FMcpAssetModifier::LoadAssetByPath<USoundAttenuation>(AssetPath, OutError);
		return OutAttenuation != nullptr;
	}

	bool ReadStringArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<FString>& OutValues, FString& OutError)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("'%s' array is required"), *FieldName);
			return false;
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			FString Value;
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetString(Value) || Value.IsEmpty())
			{
				OutError = FString::Printf(TEXT("%s[%d] must be a non-empty string"), *FieldName, Index);
				return false;
			}
			OutValues.Add(Value);
		}
		return true;
	}

	bool ReadNumberArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<float>& OutValues, FString& OutError)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values)
		{
			return true;
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			double Value = 0.0;
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Value))
			{
				OutError = FString::Printf(TEXT("%s[%d] must be a number"), *FieldName, Index);
				return false;
			}
			OutValues.Add(static_cast<float>(Value));
		}
		return true;
	}

	USoundNode* CreateWavePlayerNode(USoundCue* Cue, USoundWave* Wave, bool bLooping)
	{
		USoundNodeWavePlayer* Player = ConstructCueNode<USoundNodeWavePlayer>(Cue);
		if (!Player)
		{
			return nullptr;
		}
		Player->SetSoundWave(Wave);
		Player->bLooping = bLooping;
		return Player;
	}

	bool RebuildCueWithWave(USoundCue* Cue, USoundWave* Wave, bool bLooping, FString& OutError)
	{
		if (!Cue || !Wave)
		{
			OutError = TEXT("SoundCue and SoundWave are required");
			return false;
		}

		ResetCueGraph(Cue);
		Cue->FirstNode = CreateWavePlayerNode(Cue, Wave, bLooping);
		return FinalizeCueEdit(Cue, OutError);
	}

	bool RebuildCueWithRandomWaves(USoundCue* Cue, const TArray<USoundWave*>& Waves, const TArray<float>& Weights, bool bRandomizeWithoutReplacement, FString& OutError)
	{
		if (!Cue || Waves.Num() == 0)
		{
			OutError = TEXT("SoundCue and at least one SoundWave are required");
			return false;
		}

		if (Weights.Num() > 0 && Weights.Num() != Waves.Num())
		{
			OutError = TEXT("'weights' length must match 'sound_wave_paths' length");
			return false;
		}

		ResetCueGraph(Cue);
		USoundNodeRandom* Random = ConstructCueNode<USoundNodeRandom>(Cue);
		if (!Random)
		{
			OutError = TEXT("Failed to create SoundNodeRandom");
			return false;
		}

		TArray<USoundNode*> Children;
		for (USoundWave* Wave : Waves)
		{
			Children.Add(CreateWavePlayerNode(Cue, Wave, false));
		}
		SetNodeChildren(Random, Children);
		Random->Weights.SetNum(Waves.Num());
		Random->HasBeenUsed.SetNum(Waves.Num());
		Random->NumRandomUsed = 0;
		for (int32 Index = 0; Index < Random->Weights.Num(); ++Index)
		{
			Random->Weights[Index] = 1.0f;
			Random->HasBeenUsed[Index] = false;
		}
		for (int32 Index = 0; Index < Weights.Num() && Index < Random->Weights.Num(); ++Index)
		{
			Random->Weights[Index] = Weights[Index];
		}
		Random->bRandomizeWithoutReplacement = bRandomizeWithoutReplacement;
		Cue->FirstNode = Random;
		return FinalizeCueEdit(Cue, OutError);
	}

	bool RebuildCueWithMixerWaves(USoundCue* Cue, const TArray<USoundWave*>& Waves, const TArray<float>& Volumes, FString& OutError)
	{
		if (!Cue || Waves.Num() == 0)
		{
			OutError = TEXT("SoundCue and at least one SoundWave are required");
			return false;
		}

		if (Volumes.Num() > 0 && Volumes.Num() != Waves.Num())
		{
			OutError = TEXT("'volumes' length must match 'sound_wave_paths' length");
			return false;
		}

		ResetCueGraph(Cue);
		USoundNodeMixer* Mixer = ConstructCueNode<USoundNodeMixer>(Cue);
		if (!Mixer)
		{
			OutError = TEXT("Failed to create SoundNodeMixer");
			return false;
		}

		TArray<USoundNode*> Children;
		for (USoundWave* Wave : Waves)
		{
			Children.Add(CreateWavePlayerNode(Cue, Wave, false));
		}
		SetNodeChildren(Mixer, Children);
		for (int32 Index = 0; Index < Volumes.Num() && Index < Mixer->InputVolume.Num(); ++Index)
		{
			Mixer->InputVolume[Index] = Volumes[Index];
		}
		Cue->FirstNode = Mixer;
		return FinalizeCueEdit(Cue, OutError);
	}

	bool WrapCueWithAttenuation(USoundCue* Cue, USoundAttenuation* Attenuation, bool bOverrideAttenuation, FString& OutError)
	{
		if (!Cue || !Cue->FirstNode)
		{
			OutError = TEXT("SoundCue must have an existing root node before wrap_with_attenuation");
			return false;
		}

		USoundNode* ExistingRoot = Cue->FirstNode;
		USoundNodeAttenuation* AttenuationNode = ConstructCueNode<USoundNodeAttenuation>(Cue);
		if (!AttenuationNode)
		{
			OutError = TEXT("Failed to create SoundNodeAttenuation");
			return false;
		}
		AttenuationNode->AttenuationSettings = Attenuation;
		AttenuationNode->bOverrideAttenuation = bOverrideAttenuation;
		SetNodeChildren(AttenuationNode, { ExistingRoot });
		Cue->FirstNode = AttenuationNode;
		return FinalizeCueEdit(Cue, OutError);
	}

	bool FinalizeCueEdit(USoundCue* Cue, FString& OutError)
	{
		if (!Cue)
		{
			OutError = TEXT("SoundCue is null");
			return false;
		}

		EnsureAudioEditorLoaded();
		Cue->LinkGraphNodesFromSoundNodes();
		Cue->PostEditChange();
		FMcpAssetModifier::MarkPackageDirty(Cue);
		OutError.Reset();
		return true;
	}

	UAudioComponent* FindAudioComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TArray<UAudioComponent*> Components;
		Actor->GetComponents<UAudioComponent>(Components);
		if (!ComponentName.IsEmpty())
		{
			for (UAudioComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					return Component;
				}
			}
			return nullptr;
		}

		return Components.Num() > 0 ? Components[0] : nullptr;
	}

	UAudioComponent* CreateAudioComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		const FName NewComponentName(*(!ComponentName.IsEmpty() ? ComponentName : TEXT("AudioMCPComponent")));
		UAudioComponent* Component = NewObject<UAudioComponent>(Actor, UAudioComponent::StaticClass(), NewComponentName, RF_Transactional);
		if (!Component)
		{
			return nullptr;
		}

		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Component->SetupAttachment(Root);
		}
		else
		{
			Actor->SetRootComponent(Component);
		}

		Actor->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	bool ApplyAudioComponentSettings(
		UAudioComponent* Component,
		USoundBase* Sound,
		const TSharedPtr<FJsonObject>& Arguments,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		FString& OutError)
	{
		if (!Component)
		{
			OutError = TEXT("AudioComponent is null");
			return false;
		}

		if (Sound)
		{
			Component->SetSound(Sound);
		}

		double NumberValue = 0.0;
		if (Arguments->TryGetNumberField(TEXT("volume_multiplier"), NumberValue))
		{
			Component->SetVolumeMultiplier(static_cast<float>(NumberValue));
		}
		if (Arguments->TryGetNumberField(TEXT("pitch_multiplier"), NumberValue))
		{
			Component->SetPitchMultiplier(static_cast<float>(NumberValue));
		}

		bool BoolValue = false;
		if (Arguments->TryGetBoolField(TEXT("auto_activate"), BoolValue))
		{
			Component->SetAutoActivate(BoolValue);
		}
		if (Arguments->TryGetBoolField(TEXT("override_attenuation"), BoolValue))
		{
			Component->SetOverrideAttenuation(BoolValue);
		}

		FString AttenuationPath;
		if (Arguments->TryGetStringField(TEXT("attenuation_settings_path"), AttenuationPath))
		{
			USoundAttenuation* Attenuation = nullptr;
			if (!TryLoadAttenuation(AttenuationPath, Attenuation, OutError))
			{
				return false;
			}
			Component->SetAttenuationSettings(Attenuation);
		}

		TArray<FString> ConcurrencyPaths;
		FString ArrayError;
		if (Arguments->HasField(TEXT("concurrency_paths")) &&
			!ReadStringArray(Arguments, TEXT("concurrency_paths"), ConcurrencyPaths, ArrayError))
		{
			OutError = ArrayError;
			return false;
		}

		if (ConcurrencyPaths.Num() > 0)
		{
			Component->ConcurrencySet.Reset();
			for (const FString& ConcurrencyPath : ConcurrencyPaths)
			{
				FString LoadError;
				USoundConcurrency* Concurrency = FMcpAssetModifier::LoadAssetByPath<USoundConcurrency>(ConcurrencyPath, LoadError);
				if (!Concurrency)
				{
					OutError = LoadError;
					return false;
				}
				Component->ConcurrencySet.Add(Concurrency);
			}
		}

		Component->PostEditChange();
		OutError.Reset();
		return true;
	}
}
