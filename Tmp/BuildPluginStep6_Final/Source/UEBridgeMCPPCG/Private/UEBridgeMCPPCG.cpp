// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPPCG.h"

#include "Tools/McpToolRegistry.h"
#include "Tools/PCG/GeneratePCGScatterTool.h"

DEFINE_LOG_CATEGORY(LogUEBridgeMCPPCG);

void FUEBridgeMCPPCGModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPPCG, Log, TEXT("UEBridgeMCPPCG module starting up"));

	FMcpToolRegistry& Registry = FMcpToolRegistry::Get();
	Registry.RegisterToolClass(UGeneratePCGScatterTool::StaticClass());
	Registry.FindTool(TEXT("generate-pcg-scatter"));
}

void FUEBridgeMCPPCGModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCPPCG, Log, TEXT("UEBridgeMCPPCG module shutting down"));

	if (FModuleManager::Get().IsModuleLoaded(TEXT("UEBridgeMCP")))
	{
		FMcpToolRegistry::Get().UnregisterTool(TEXT("generate-pcg-scatter"));
	}
}

IMPLEMENT_MODULE(FUEBridgeMCPPCGModule, UEBridgeMCPPCG)
