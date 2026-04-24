// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Audio/CreateAudioComponentSetupTool.h"

#include "Tools/Audio/AudioToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Sound/SoundBase.h"

FString UCreateAudioComponentSetupTool::GetToolDescription() const
{
	return TEXT("Create or update an AudioComponent setup on an editor or PIE actor without starting playback.");
}

TMap<FString, FMcpSchemaProperty> UCreateAudioComponentSetupTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing or new AudioComponent name")));
	Schema.Add(TEXT("sound_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional SoundBase asset path to assign")));
	Schema.Add(TEXT("create_if_missing"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create AudioComponent when no matching component exists")));
	Schema.Add(TEXT("auto_activate"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Set component auto-activation")));
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

FMcpToolResult UCreateAudioComponentSetupTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const FString SoundPath = GetStringArgOrDefault(Arguments, TEXT("sound_path"));
	const bool bCreateIfMissing = GetBoolArgOrDefault(Arguments, TEXT("create_if_missing"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	USoundBase* Sound = nullptr;
	if (!SoundPath.IsEmpty())
	{
		FString LoadError;
		if (!AudioToolUtils::TryLoadSoundBase(SoundPath, Sound, LoadError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}
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
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Audio Component Setup")));
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
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Audio component setup dry run complete"));
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
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Audio component setup applied"));
}
