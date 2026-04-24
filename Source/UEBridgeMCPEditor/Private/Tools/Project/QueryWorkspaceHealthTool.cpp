// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Project/QueryWorkspaceHealthTool.h"

#include "Subsystem/McpEditorSubsystem.h"
#include "Tools/McpToolRegistry.h"
#include "UEBridgeMCP.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpOptionalCapabilityUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace
{
	TSharedPtr<FJsonObject> MakePathState(const FString& Path, const bool bDirectory)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("path"), Path);
		Object->SetBoolField(TEXT("exists"), bDirectory
			? IFileManager::Get().DirectoryExists(*Path)
			: IFileManager::Get().FileExists(*Path));
		return Object;
	}

	int32 CountActors(UWorld* World)
	{
		if (!World)
		{
			return 0;
		}

		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}

	TSharedPtr<FJsonObject> BuildWorldSummary(const FString& WorldType, UWorld* World)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("type"), WorldType);
		Object->SetBoolField(TEXT("available"), World != nullptr);
		if (World)
		{
			Object->SetStringField(TEXT("name"), World->GetName());
			Object->SetStringField(TEXT("path"), World->GetPathName());
			Object->SetStringField(TEXT("map_name"), World->GetMapName());
			Object->SetNumberField(TEXT("actor_count"), CountActors(World));
			if (World->PersistentLevel && World->PersistentLevel->GetPackage())
			{
				Object->SetStringField(TEXT("package_name"), World->PersistentLevel->GetPackage()->GetName());
				Object->SetBoolField(TEXT("dirty"), World->PersistentLevel->GetPackage()->IsDirty());
			}
		}
		return Object;
	}

	TSharedPtr<FJsonObject> BuildServerSummary()
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetBoolField(TEXT("editor_available"), GEditor != nullptr);
		if (!GEditor)
		{
			Object->SetBoolField(TEXT("running"), false);
			return Object;
		}

		UMcpEditorSubsystem* McpSubsystem = GEditor->GetEditorSubsystem<UMcpEditorSubsystem>();
		Object->SetBoolField(TEXT("subsystem_available"), McpSubsystem != nullptr);
		if (!McpSubsystem)
		{
			Object->SetBoolField(TEXT("running"), false);
			return Object;
		}

		UMcpServerSettings* Settings = McpSubsystem->GetSettings();
		if (Settings)
		{
			Object->SetNumberField(TEXT("configured_port"), Settings->ServerPort);
			Object->SetStringField(TEXT("bind_address"), Settings->BindAddress);
			Object->SetBoolField(TEXT("auto_start"), Settings->bAutoStartServer);
		}
		Object->SetBoolField(TEXT("running"), McpSubsystem->IsServerRunning());
		Object->SetNumberField(TEXT("actual_port"), McpSubsystem->GetActualPort());
		Object->SetNumberField(TEXT("registered_tools"), FMcpToolRegistry::Get().GetToolCount());
		return Object;
	}

	TSharedPtr<FJsonObject> BuildPluginSummary()
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), TEXT("UEBridgeMCP"));
		Object->SetStringField(TEXT("version"), UEBRIDGEMCP_VERSION);

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UEBridgeMCP"));
		Object->SetBoolField(TEXT("found"), Plugin.IsValid());
		if (Plugin.IsValid())
		{
			Object->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
			Object->SetStringField(TEXT("friendly_name"), Plugin->GetFriendlyName());
			Object->SetStringField(TEXT("base_dir"), Plugin->GetBaseDir());
			Object->SetBoolField(TEXT("can_contain_content"), Plugin->CanContainContent());
			Object->SetBoolField(TEXT("mounted"), Plugin->IsMounted());
		}
		return Object;
	}

	TSharedPtr<FJsonObject> BuildDirtyPackages(const int32 MaxDirtyPackages)
	{
		TArray<TSharedPtr<FJsonValue>> Packages;
		int32 DirtyCount = 0;
		bool bTruncated = false;

		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (!Package || !Package->IsDirty() || Package->HasAnyFlags(RF_Transient))
			{
				continue;
			}

			++DirtyCount;
			if (Packages.Num() >= MaxDirtyPackages)
			{
				bTruncated = true;
				continue;
			}

			TSharedPtr<FJsonObject> PackageObject = MakeShareable(new FJsonObject);
			PackageObject->SetStringField(TEXT("package_name"), Package->GetName());
			PackageObject->SetBoolField(TEXT("contains_map"), Package->ContainsMap());
			Packages.Add(MakeShareable(new FJsonValueObject(PackageObject)));
		}

		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("count"), DirtyCount);
		Object->SetBoolField(TEXT("truncated"), bTruncated);
		Object->SetArrayField(TEXT("packages"), Packages);
		return Object;
	}
}

FString UQueryWorkspaceHealthTool::GetToolDescription() const
{
	return TEXT("Return a read-only workspace health snapshot: project paths, plugin/server state, worlds, optional capabilities, and dirty packages.");
}

TMap<FString, FMcpSchemaProperty> UQueryWorkspaceHealthTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("include_dirty_packages"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include dirty package count and sample list. Default: true.")));
	Schema.Add(TEXT("include_optional_capabilities"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include optional editor capability flags. Default: true.")));
	Schema.Add(TEXT("include_validation_paths"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include common validation directory existence checks. Default: true.")));
	Schema.Add(TEXT("max_dirty_packages"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum dirty package names to return. Default: 50, max: 500.")));
	return Schema;
}

FMcpToolResult UQueryWorkspaceHealthTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const bool bIncludeDirtyPackages = GetBoolArgOrDefault(Arguments, TEXT("include_dirty_packages"), true);
	const bool bIncludeOptionalCapabilities = GetBoolArgOrDefault(Arguments, TEXT("include_optional_capabilities"), true);
	const bool bIncludeValidationPaths = GetBoolArgOrDefault(Arguments, TEXT("include_validation_paths"), true);
	const int32 MaxDirtyPackages = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("max_dirty_packages"), 50), 0, 500);

	const FString ProjectFilePath = FPaths::GetProjectFilePath();
	const FString ProjectDir = FPaths::ProjectDir();
	const FString ContentDir = FPaths::ProjectContentDir();
	const FString SourceDir = FPaths::Combine(ProjectDir, TEXT("Source"));

	TSharedPtr<FJsonObject> ProjectObject = MakeShareable(new FJsonObject);
	ProjectObject->SetStringField(TEXT("name"), FApp::GetProjectName());
	ProjectObject->SetStringField(TEXT("project_file"), ProjectFilePath);
	ProjectObject->SetStringField(TEXT("project_directory"), ProjectDir);
	ProjectObject->SetStringField(TEXT("content_directory"), ContentDir);
	ProjectObject->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

	TSharedPtr<FJsonObject> PathsObject = MakeShareable(new FJsonObject);
	PathsObject->SetObjectField(TEXT("project_file"), MakePathState(ProjectFilePath, false));
	PathsObject->SetObjectField(TEXT("project_directory"), MakePathState(ProjectDir, true));
	PathsObject->SetObjectField(TEXT("content_directory"), MakePathState(ContentDir, true));
	PathsObject->SetObjectField(TEXT("source_directory"), MakePathState(SourceDir, true));

	if (bIncludeValidationPaths)
	{
		TSharedPtr<FJsonObject> ValidationObject = MakeShareable(new FJsonObject);
		ValidationObject->SetObjectField(TEXT("blueprint_round1"), MakePathState(FPaths::Combine(ContentDir, TEXT("UEBridgeMCPValidation/BlueprintRound1")), true));
		ValidationObject->SetObjectField(TEXT("blueprint_round2"), MakePathState(FPaths::Combine(ContentDir, TEXT("UEBridgeMCPValidation/BlueprintRound2")), true));
		ValidationObject->SetObjectField(TEXT("macro_utility_round1"), MakePathState(FPaths::Combine(ContentDir, TEXT("UEBridgeMCPValidation/MacroUtilityRound1")), true));
		PathsObject->SetObjectField(TEXT("validation"), ValidationObject);
	}

	TSharedPtr<FJsonObject> WorldsObject = MakeShareable(new FJsonObject);
	UWorld* EditorWorld = FMcpAssetModifier::ResolveWorld(TEXT("editor"));
	WorldsObject->SetObjectField(TEXT("editor"), BuildWorldSummary(TEXT("editor"), EditorWorld));
	WorldsObject->SetObjectField(TEXT("pie"), BuildWorldSummary(TEXT("pie"), FMcpAssetModifier::ResolveWorld(TEXT("pie"))));

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	if (!EditorWorld)
	{
		TSharedPtr<FJsonObject> Warning = MakeShareable(new FJsonObject);
		Warning->SetStringField(TEXT("code"), TEXT("UEBMCP_WORLD_NOT_AVAILABLE"));
		Warning->SetStringField(TEXT("message"), TEXT("No editor world is currently available"));
		WarningsArray.Add(MakeShareable(new FJsonValueObject(Warning)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("project"), ProjectObject);
	Response->SetObjectField(TEXT("paths"), PathsObject);
	Response->SetObjectField(TEXT("plugin"), BuildPluginSummary());
	Response->SetObjectField(TEXT("mcp_server"), BuildServerSummary());
	Response->SetObjectField(TEXT("worlds"), WorldsObject);
	if (bIncludeOptionalCapabilities)
	{
		Response->SetObjectField(TEXT("optional_capabilities"), FMcpOptionalCapabilityUtils::BuildOptionalCapabilities(EditorWorld));
	}
	if (bIncludeDirtyPackages)
	{
		Response->SetObjectField(TEXT("dirty_packages"), BuildDirtyPackages(MaxDirtyPackages));
	}
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
	return FMcpToolResult::StructuredSuccess(Response, TEXT("Workspace health snapshot ready"));
}
