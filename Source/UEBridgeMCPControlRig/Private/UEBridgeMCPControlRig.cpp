// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPControlRig.h"

#include "Tools/McpToolRegistry.h"
#include "Tools/ControlRig/EditControlRigGraphTool.h"

DEFINE_LOG_CATEGORY(LogUEBridgeMCPControlRig);

void FUEBridgeMCPControlRigModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPControlRig, Log, TEXT("UEBridgeMCPControlRig module starting up"));

	FMcpToolRegistry& Registry = FMcpToolRegistry::Get();
	Registry.RegisterToolClass(UEditControlRigGraphTool::StaticClass());
	Registry.FindTool(TEXT("edit-control-rig-graph"));
}

void FUEBridgeMCPControlRigModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCPControlRig, Log, TEXT("UEBridgeMCPControlRig module shutting down"));

	if (FModuleManager::Get().IsModuleLoaded(TEXT("UEBridgeMCP")))
	{
		FMcpToolRegistry::Get().UnregisterTool(TEXT("edit-control-rig-graph"));
	}
}

IMPLEMENT_MODULE(FUEBridgeMCPControlRigModule, UEBridgeMCPControlRig)
