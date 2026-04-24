// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/BuiltInWorkflowContent.h"

#include "Protocol/McpPromptRegistry.h"
#include "Protocol/McpResourceRegistry.h"
#include "Protocol/McpResourcePromptTypes.h"
#include "Tools/Workflow/WorkflowPresetUtils.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FBuiltInResourceSpec
	{
		const TCHAR* FileName;
		const TCHAR* Uri;
		const TCHAR* Name;
		const TCHAR* Description;
		const TCHAR* MimeType;
	};

	const TCHAR* ResourcesDirectory = TEXT("Resources/MCP/Resources");
	const TCHAR* PromptsDirectory = TEXT("Resources/MCP/Prompts");

	const TArray<FBuiltInResourceSpec> ResourceSpecs = {
		{ TEXT("AnimationSmokeChecklist.md"), TEXT("uebmcp://builtin/resources/animation-smoke-checklist"), TEXT("Animation Smoke Checklist"), TEXT("Checklist for validating animation authoring and regression smoke flows."), TEXT("text/markdown") },
		{ TEXT("SequencerEditRecipe.md"), TEXT("uebmcp://builtin/resources/sequencer-edit-recipe"), TEXT("Sequencer Edit Recipe"), TEXT("Recipe for minimal, safe sequence editing and validation."), TEXT("text/markdown") },
		{ TEXT("WorldProductionRecipe.md"), TEXT("uebmcp://builtin/resources/world-production-recipe"), TEXT("World Production Recipe"), TEXT("World production recipe for spline, foliage, landscape, and world partition work."), TEXT("text/markdown") },
		{ TEXT("PerformanceTriageGuide.md"), TEXT("uebmcp://builtin/resources/performance-triage-guide"), TEXT("Performance Triage Guide"), TEXT("Guide for editor and PIE performance triage and evidence capture."), TEXT("text/markdown") },
		{ TEXT("ExternalContentSafetyGuide.md"), TEXT("uebmcp://builtin/resources/external-content-safety-guide"), TEXT("External Content Safety Guide"), TEXT("Safety and provenance guidance for external content generation workflows."), TEXT("text/markdown") }
	};

	FString GetPluginBaseDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UEBridgeMCP"));
		return Plugin.IsValid() ? Plugin->GetBaseDir() : FString();
	}

	FString ReadTextFile(const FString& FilePath)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *FilePath);
		return Text;
	}

	bool BuildPromptMessagesFromTemplate(
		const FString& TemplatePath,
		const TSharedPtr<FJsonObject>& Arguments,
		FMcpPromptGetResult& OutResult,
		FString& OutError)
	{
		FString PromptJsonString;
		if (!FFileHelper::LoadFileToString(PromptJsonString, *TemplatePath))
		{
			OutError = FString::Printf(TEXT("Failed to load prompt template: %s"), *TemplatePath);
			return false;
		}

		TSharedPtr<FJsonObject> PromptJson;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PromptJsonString);
		if (!FJsonSerializer::Deserialize(Reader, PromptJson) || !PromptJson.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse prompt template: %s"), *TemplatePath);
			return false;
		}

		PromptJson->TryGetStringField(TEXT("name"), OutResult.Name);
		PromptJson->TryGetStringField(TEXT("description"), OutResult.Description);

		const TArray<TSharedPtr<FJsonValue>>* MessagesArray = nullptr;
		if (!PromptJson->TryGetArrayField(TEXT("messages"), MessagesArray) || !MessagesArray)
		{
			OutError = FString::Printf(TEXT("Prompt template has no messages: %s"), *TemplatePath);
			return false;
		}

		OutResult.Messages.Reset();
		for (const TSharedPtr<FJsonValue>& MessageValue : *MessagesArray)
		{
			const TSharedPtr<FJsonObject>* MessageObject = nullptr;
			if (!MessageValue.IsValid() || !MessageValue->TryGetObject(MessageObject) || !MessageObject || !(*MessageObject).IsValid())
			{
				continue;
			}

			FMcpPromptMessage Message;
			(*MessageObject)->TryGetStringField(TEXT("role"), Message.Role);
			FString TemplateText;
			(*MessageObject)->TryGetStringField(TEXT("template"), TemplateText);
			Message.Text = WorkflowPresetUtils::RenderTemplateString(TemplateText, Arguments);
			OutResult.Messages.Add(Message);
		}

		return true;
	}
}

namespace BuiltInWorkflowContent
{
	void RegisterBuiltInResourcesAndPrompts()
	{
		const FString PluginBaseDir = GetPluginBaseDir();
		if (PluginBaseDir.IsEmpty())
		{
			return;
		}

		FMcpResourceRegistry::Get().ClearAllResources();
		FMcpPromptRegistry::Get().ClearAllPrompts();

		const FString ResourceBaseDir = PluginBaseDir / ResourcesDirectory;
		for (const FBuiltInResourceSpec& Spec : ResourceSpecs)
		{
			const FString FilePath = ResourceBaseDir / Spec.FileName;

			FMcpResourceDefinition Definition;
			Definition.Uri = Spec.Uri;
			Definition.Name = Spec.Name;
			Definition.Description = Spec.Description;
			Definition.MimeType = Spec.MimeType;
			Definition.ReadCallback = [FilePath, Spec](FMcpResourceReadResult& OutResult, FString& OutError)
			{
				FString Text;
				if (!FFileHelper::LoadFileToString(Text, *FilePath))
				{
					OutError = FString::Printf(TEXT("Failed to load resource file: %s"), *FilePath);
					return false;
				}

				FMcpResourceContent Content;
				Content.Uri = Spec.Uri;
				Content.Name = Spec.Name;
				Content.Description = Spec.Description;
				Content.MimeType = Spec.MimeType;
				Content.Text = Text;
				OutResult.Contents = { Content };
				return true;
			};
			FMcpResourceRegistry::Get().RegisterResource(Definition);
		}

		TArray<FString> PromptFiles;
		IFileManager::Get().FindFiles(PromptFiles, *((PluginBaseDir / PromptsDirectory) / TEXT("*.json")), true, false);
		for (const FString& PromptFileName : PromptFiles)
		{
			const FString PromptPath = PluginBaseDir / PromptsDirectory / PromptFileName;
			const FString PromptJsonString = ReadTextFile(PromptPath);
			if (PromptJsonString.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> PromptJson;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PromptJsonString);
			if (!FJsonSerializer::Deserialize(Reader, PromptJson) || !PromptJson.IsValid())
			{
				continue;
			}

			FMcpPromptDefinition Definition;
			if (!PromptJson->TryGetStringField(TEXT("name"), Definition.Name) || Definition.Name.IsEmpty())
			{
				continue;
			}
			PromptJson->TryGetStringField(TEXT("description"), Definition.Description);

			const TArray<TSharedPtr<FJsonValue>>* ArgumentsArray = nullptr;
			if (PromptJson->TryGetArrayField(TEXT("arguments"), ArgumentsArray) && ArgumentsArray)
			{
				for (const TSharedPtr<FJsonValue>& ArgumentValue : *ArgumentsArray)
				{
					const TSharedPtr<FJsonObject>* ArgumentObject = nullptr;
					if (!ArgumentValue.IsValid() || !ArgumentValue->TryGetObject(ArgumentObject) || !ArgumentObject || !(*ArgumentObject).IsValid())
					{
						continue;
					}

					FMcpPromptArgumentDefinition Argument;
					(*ArgumentObject)->TryGetStringField(TEXT("name"), Argument.Name);
					(*ArgumentObject)->TryGetStringField(TEXT("description"), Argument.Description);
					(*ArgumentObject)->TryGetBoolField(TEXT("required"), Argument.bRequired);
					if (!Argument.Name.IsEmpty())
					{
						Definition.Arguments.Add(Argument);
					}
				}
			}

			Definition.BuildCallback = [PromptPath](const TSharedPtr<FJsonObject>& Arguments, FMcpPromptGetResult& OutResult, FString& OutError)
			{
				return BuildPromptMessagesFromTemplate(PromptPath, Arguments, OutResult, OutError);
			};

			FMcpPromptRegistry::Get().RegisterPrompt(Definition);
		}
	}
}
