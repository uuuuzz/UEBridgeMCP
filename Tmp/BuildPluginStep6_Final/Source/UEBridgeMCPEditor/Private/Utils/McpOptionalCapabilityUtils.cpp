// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Utils/McpOptionalCapabilityUtils.h"

#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

bool FMcpOptionalCapabilityUtils::IsPluginEnabled(const TCHAR* PluginName)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	return Plugin.IsValid() && Plugin->IsEnabled();
}

bool FMcpOptionalCapabilityUtils::HasModule(const TCHAR* ModuleName)
{
	return FModuleManager::Get().ModuleExists(ModuleName);
}

bool FMcpOptionalCapabilityUtils::IsSequencerAvailable()
{
	return HasModule(TEXT("LevelSequence")) && HasModule(TEXT("MovieSceneTracks"));
}

bool FMcpOptionalCapabilityUtils::IsLandscapeAvailable()
{
	return HasModule(TEXT("Landscape"));
}

bool FMcpOptionalCapabilityUtils::IsFoliageAvailable()
{
	return HasModule(TEXT("Foliage"));
}

bool FMcpOptionalCapabilityUtils::IsWorldPartitionAvailable()
{
	return HasModule(TEXT("Engine"));
}

bool FMcpOptionalCapabilityUtils::IsControlRigAvailable()
{
	return IsPluginEnabled(TEXT("ControlRig")) &&
		HasModule(TEXT("ControlRigDeveloper")) &&
		HasModule(TEXT("RigVMDeveloper"));
}

bool FMcpOptionalCapabilityUtils::IsPCGAvailable()
{
	return IsPluginEnabled(TEXT("PCG")) && HasModule(TEXT("PCG"));
}

bool FMcpOptionalCapabilityUtils::IsExternalAIAvailable()
{
	return FModuleManager::Get().ModuleExists(TEXT("UEBridgeMCPExternalAI")) &&
		FModuleManager::Get().IsModuleLoaded(TEXT("UEBridgeMCPExternalAI"));
}

TSharedPtr<FJsonObject> FMcpOptionalCapabilityUtils::BuildOptionalCapabilities(UWorld* World)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("sequencer_available"), IsSequencerAvailable());
	Result->SetBoolField(TEXT("landscape_available"), IsLandscapeAvailable());
	Result->SetBoolField(TEXT("foliage_available"), IsFoliageAvailable());
	Result->SetBoolField(TEXT("control_rig_available"), IsControlRigAvailable());
	Result->SetBoolField(TEXT("pcg_available"), IsPCGAvailable());
	Result->SetBoolField(TEXT("external_ai_available"), IsExternalAIAvailable());
	Result->SetBoolField(TEXT("world_partition_available"), World ? (World->GetWorldPartition() != nullptr) : IsWorldPartitionAvailable());
	return Result;
}
