// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Audio/ApplyAudioToActorTool.h"

#include "Tools/Audio/AudioToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Sound/SoundBase.h"

FString UApplyAudioToActorTool::GetToolDescription() const
{
	return TEXT("Apply a SoundBase asset to an actor by creating or updating an AudioComponent, with optional playback outside editor-world simulation.");
}

TMap<FString, FMcpSchemaProperty> UApplyAudioToActorTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("sound_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("SoundWave, SoundCue, or SoundBase asset path"), true));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing or new AudioComponent name")));
	Schema.Add(TEXT("create_if_missing"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create AudioComponent when no matching component exists")));
	Schema.Add(TEXT("auto_activate"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Set component auto-activation")));
	Schema.Add(TEXT("play_now"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Start playback after applying when safe")));
	Schema.Add(TEXT("start_time"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Playback start time in seconds")));
	Schema.Add(TEXT("volume_multiplier"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Component volume multiplier")));
	Schema.Add(TEXT("pitch_multiplier"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Component pitch multiplier")));
	Schema.Add(TEXT("attenuation_settings_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional SoundAttenuation asset path")));
	Schema.Add(TEXT("override_attenuation"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Set component attenuation override flag")));
	Schema.Add(TEXT("concurrency_paths"), FMcpSchemaProperty::Make(TEXT("array"), TEXT("Optional SoundConcurrency asset paths")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without mutating the actor")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited map when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on failure")));
	return Schema;
}

FMcpToolResult UApplyAudioToActorTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString SoundPath = GetStringArgOrDefault(Arguments, TEXT("sound_path"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bCreateIfMissing = GetBoolArgOrDefault(Arguments, TEXT("create_if_missing"), true);
	const bool bPlayNow = GetBoolArgOrDefault(Arguments, TEXT("play_now"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const float StartTime = GetFloatArgOrDefault(Arguments, TEXT("start_time"), 0.0f);

	FString LoadError;
	USoundBase* Sound = nullptr;
	if (!AudioToolUtils::TryLoadSoundBase(SoundPath, Sound, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UWorld* World = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	AActor* Actor = LevelActorToolUtils::ResolveActorReference(
		Arguments,
		WorldType,
		TEXT("actor_name"),
		TEXT("actor_handle"),
		Context,
		World,
		ErrorCode,
		ErrorMessage,
		ErrorDetails,
		true);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Apply Audio To Actor")));
		Actor->Modify();
	}

	UAudioComponent* Component = AudioToolUtils::FindAudioComponent(Actor, ComponentName);
	const bool bWouldCreateComponent = !Component && bCreateIfMissing;
	if (!Component && !bDryRun && bCreateIfMissing)
	{
		Component = AudioToolUtils::CreateAudioComponent(Actor, ComponentName);
	}

	if (!Component && !bWouldCreateComponent)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), TEXT("AudioComponent not found and create_if_missing is false"));
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Result->SetStringField(TEXT("component_name"), Component ? Component->GetName() : (ComponentName.IsEmpty() ? TEXT("AudioMCPComponent") : ComponentName));
		Result->SetBoolField(TEXT("would_create_component"), bWouldCreateComponent);
		Result->SetStringField(TEXT("sound_path"), SoundPath);
		Result->SetBoolField(TEXT("would_play_now"), bPlayNow);
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Audio apply dry run complete"));
	}

	TArray<TSharedPtr<FJsonValue>> Warnings;
	FString ApplyError;
	if (!AudioToolUtils::ApplyAudioComponentSettings(Component, Sound, Arguments, Warnings, ApplyError))
	{
		if (Transaction.IsValid() && bRollbackOnError)
		{
			Transaction->Cancel();
			Transaction.Reset();
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), ApplyError);
	}

	bool bPlaybackApplied = false;
	bool bPlaybackDeferred = false;
	if (bPlayNow)
	{
		if (World && World->WorldType == EWorldType::Editor)
		{
			bPlaybackDeferred = true;
			Warnings.Add(MakeShareable(new FJsonValueString(TEXT("play_now was requested but skipped in the editor world; the AudioComponent setup was applied without starting playback."))));
		}
		else
		{
			Component->Play(StartTime);
			bPlaybackApplied = true;
		}
	}

	Actor->PostEditChange();

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	if (World)
	{
		FString SaveErrorCode;
		FString SaveErrorMessage;
		if (!LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, Warnings, ModifiedAssets, SaveErrorCode, SaveErrorMessage) && bRollbackOnError)
		{
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();
			}
			return FMcpToolResult::StructuredError(SaveErrorCode, SaveErrorMessage);
		}
	}

	TSharedPtr<FJsonObject> Result = AudioToolUtils::SerializeAudioComponent(Component);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("dry_run"), false);
	Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
	Result->SetBoolField(TEXT("component_created"), bWouldCreateComponent);
	Result->SetBoolField(TEXT("play_now"), bPlayNow);
	Result->SetBoolField(TEXT("playback_applied"), bPlaybackApplied);
	Result->SetBoolField(TEXT("playback_deferred"), bPlaybackDeferred);
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Audio applied to actor"));
}
