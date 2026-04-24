// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace WorkflowPresetUtils
{
	FString GetWorkflowPresetDirectory();
	FString GetWorkflowPresetPath(const FString& PresetId);
	bool ValidatePresetId(const FString& PresetId, FString& OutError);
	bool ValidatePresetObject(const TSharedPtr<FJsonObject>& PresetObject, FString& OutError);

	bool SavePreset(const TSharedPtr<FJsonObject>& PresetObject, FString& OutPresetPath, FString& OutError);
	bool LoadPreset(const FString& PresetId, TSharedPtr<FJsonObject>& OutPresetObject, FString& OutPresetPath, FString& OutError);
	bool DeletePreset(const FString& PresetId, FString& OutPresetPath, FString& OutError);
	bool ListPresets(TArray<TSharedPtr<FJsonObject>>& OutPresets, FString& OutError);

	FString JsonValueToString(const TSharedPtr<FJsonValue>& Value);
	FString RenderTemplateString(const FString& TemplateText, const TSharedPtr<FJsonObject>& Arguments);
	TSharedPtr<FJsonValue> ResolveTemplates(const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>& Arguments);
	TSharedPtr<FJsonObject> MergeArguments(const TSharedPtr<FJsonObject>& Defaults, const TSharedPtr<FJsonObject>& Overrides);
}
