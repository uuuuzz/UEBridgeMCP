// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/WorkflowPresetUtils.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	bool EnsureDirectoryExists(const FString& DirectoryPath, FString& OutError)
	{
		if (IFileManager::Get().MakeDirectory(*DirectoryPath, true))
		{
			return true;
		}

		if (IFileManager::Get().DirectoryExists(*DirectoryPath))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Failed to create preset directory: %s"), *DirectoryPath);
		return false;
	}

	bool SerializeJsonToFile(const TSharedPtr<FJsonObject>& JsonObject, const FString& FilePath, FString& OutError)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
		{
			OutError = TEXT("Failed to serialize preset JSON");
			return false;
		}

		if (!FFileHelper::SaveStringToFile(Output, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to save preset file: %s"), *FilePath);
			return false;
		}

		return true;
	}

	bool LoadJsonFromFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJsonObject, FString& OutError)
	{
		FString Input;
		if (!FFileHelper::LoadFileToString(Input, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read preset file: %s"), *FilePath);
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
		if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse preset JSON: %s"), *FilePath);
			return false;
		}

		return true;
	}
}

namespace WorkflowPresetUtils
{
	FString GetWorkflowPresetDirectory()
	{
		return FPaths::ProjectConfigDir() / TEXT("UEBridgeMCP/WorkflowPresets");
	}

	FString GetWorkflowPresetPath(const FString& PresetId)
	{
		return GetWorkflowPresetDirectory() / FString::Printf(TEXT("%s.json"), *PresetId);
	}

	bool ValidatePresetId(const FString& PresetId, FString& OutError)
	{
		if (PresetId.IsEmpty())
		{
			OutError = TEXT("Preset 'id' is required");
			return false;
		}

		for (const TCHAR Character : PresetId)
		{
			const bool bAllowed = FChar::IsAlnum(Character) || Character == TEXT('-') || Character == TEXT('_');
			if (!bAllowed)
			{
				OutError = TEXT("Preset 'id' may only contain letters, numbers, '-' and '_'");
				return false;
			}
		}

		return true;
	}

	bool ValidatePresetObject(const TSharedPtr<FJsonObject>& PresetObject, FString& OutError)
	{
		if (!PresetObject.IsValid())
		{
			OutError = TEXT("Preset object is required");
			return false;
		}

		FString PresetId;
		if (!PresetObject->TryGetStringField(TEXT("id"), PresetId) || !ValidatePresetId(PresetId, OutError))
		{
			return false;
		}

		FString Title;
		if (!PresetObject->TryGetStringField(TEXT("title"), Title) || Title.IsEmpty())
		{
			OutError = TEXT("Preset 'title' is required");
			return false;
		}

		if (!PresetObject->HasField(TEXT("description")))
		{
			OutError = TEXT("Preset 'description' is required");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ResourceUris = nullptr;
		if (!PresetObject->TryGetArrayField(TEXT("resource_uris"), ResourceUris) || !ResourceUris)
		{
			OutError = TEXT("Preset 'resource_uris' array is required");
			return false;
		}

		FString PromptName;
		if (!PresetObject->TryGetStringField(TEXT("prompt_name"), PromptName))
		{
			OutError = TEXT("Preset 'prompt_name' is required");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
		if (!PresetObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls) || !ToolCalls)
		{
			OutError = TEXT("Preset 'tool_calls' array is required");
			return false;
		}

		const TSharedPtr<FJsonObject>* DefaultArguments = nullptr;
		if (!PresetObject->TryGetObjectField(TEXT("default_arguments"), DefaultArguments) || !DefaultArguments || !(*DefaultArguments).IsValid())
		{
			OutError = TEXT("Preset 'default_arguments' object is required");
			return false;
		}

		return true;
	}

	bool SavePreset(const TSharedPtr<FJsonObject>& PresetObject, FString& OutPresetPath, FString& OutError)
	{
		if (!ValidatePresetObject(PresetObject, OutError))
		{
			return false;
		}

		const FString DirectoryPath = GetWorkflowPresetDirectory();
		if (!EnsureDirectoryExists(DirectoryPath, OutError))
		{
			return false;
		}

		const FString PresetId = PresetObject->GetStringField(TEXT("id"));
		OutPresetPath = GetWorkflowPresetPath(PresetId);
		return SerializeJsonToFile(PresetObject, OutPresetPath, OutError);
	}

	bool LoadPreset(const FString& PresetId, TSharedPtr<FJsonObject>& OutPresetObject, FString& OutPresetPath, FString& OutError)
	{
		if (!ValidatePresetId(PresetId, OutError))
		{
			return false;
		}

		OutPresetPath = GetWorkflowPresetPath(PresetId);
		if (!FPaths::FileExists(OutPresetPath))
		{
			OutError = FString::Printf(TEXT("Preset not found: %s"), *PresetId);
			return false;
		}

		if (!LoadJsonFromFile(OutPresetPath, OutPresetObject, OutError))
		{
			return false;
		}

		return ValidatePresetObject(OutPresetObject, OutError);
	}

	bool DeletePreset(const FString& PresetId, FString& OutPresetPath, FString& OutError)
	{
		if (!ValidatePresetId(PresetId, OutError))
		{
			return false;
		}

		OutPresetPath = GetWorkflowPresetPath(PresetId);
		if (!FPaths::FileExists(OutPresetPath))
		{
			OutError = FString::Printf(TEXT("Preset not found: %s"), *PresetId);
			return false;
		}

		if (!IFileManager::Get().Delete(*OutPresetPath))
		{
			OutError = FString::Printf(TEXT("Failed to delete preset file: %s"), *OutPresetPath);
			return false;
		}

		return true;
	}

	bool ListPresets(TArray<TSharedPtr<FJsonObject>>& OutPresets, FString& OutError)
	{
		OutPresets.Reset();

		TArray<FString> PresetFiles;
		IFileManager::Get().FindFiles(PresetFiles, *(GetWorkflowPresetDirectory() / TEXT("*.json")), true, false);

		for (const FString& PresetFileName : PresetFiles)
		{
			TSharedPtr<FJsonObject> PresetObject;
			const FString FullPath = GetWorkflowPresetDirectory() / PresetFileName;
			if (!LoadJsonFromFile(FullPath, PresetObject, OutError))
			{
				return false;
			}

			OutPresets.Add(PresetObject);
		}

		return true;
	}

	FString JsonValueToString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("");
		}

		switch (Value->Type)
		{
		case EJson::String:
			return Value->AsString();
		case EJson::Number:
			return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Array:
		case EJson::Object:
		case EJson::Null:
		default:
		{
			FString Serialized;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
			FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT(""), Writer);
			return Serialized;
		}
		}
	}

	FString RenderTemplateString(const FString& TemplateText, const TSharedPtr<FJsonObject>& Arguments)
	{
		FString Output = TemplateText;
		if (!Arguments.IsValid())
		{
			return Output;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
		{
			Output = Output.Replace(
				*FString::Printf(TEXT("{{%s}}"), *Pair.Key),
				*JsonValueToString(Pair.Value),
				ESearchCase::IgnoreCase);
			Output = Output.Replace(
				*FString::Printf(TEXT("{%s}"), *Pair.Key),
				*JsonValueToString(Pair.Value),
				ESearchCase::IgnoreCase);
		}

		return Output;
	}

	TSharedPtr<FJsonValue> ResolveTemplates(const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>& Arguments)
	{
		if (!Value.IsValid())
		{
			return MakeShareable(new FJsonValueNull());
		}

		switch (Value->Type)
		{
		case EJson::String:
			return MakeShareable(new FJsonValueString(RenderTemplateString(Value->AsString(), Arguments)));

		case EJson::Array:
		{
			TArray<TSharedPtr<FJsonValue>> OutputArray;
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				OutputArray.Add(ResolveTemplates(Item, Arguments));
			}
			return MakeShareable(new FJsonValueArray(OutputArray));
		}

		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> InputObject = Value->AsObject();
			TSharedPtr<FJsonObject> OutputObject = MakeShareable(new FJsonObject);
			if (InputObject.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InputObject->Values)
				{
					OutputObject->SetField(Pair.Key, ResolveTemplates(Pair.Value, Arguments));
				}
			}
			return MakeShareable(new FJsonValueObject(OutputObject));
		}

		default:
			return Value;
		}
	}

	TSharedPtr<FJsonObject> MergeArguments(const TSharedPtr<FJsonObject>& Defaults, const TSharedPtr<FJsonObject>& Overrides)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

		if (Defaults.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Defaults->Values)
			{
				Result->SetField(Pair.Key, Pair.Value);
			}
		}

		if (Overrides.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Overrides->Values)
			{
				Result->SetField(Pair.Key, Pair.Value);
			}
		}

		return Result;
	}
}
