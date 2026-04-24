// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPExternalAI.h"

#include "Tools/McpToolRegistry.h"
#include "Tools/ExternalAI/GenerateExternalContentTool.h"

DEFINE_LOG_CATEGORY(LogUEBridgeMCPExternalAI);

void FUEBridgeMCPExternalAIModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPExternalAI, Log, TEXT("UEBridgeMCPExternalAI module starting up"));

	FMcpToolRegistry& Registry = FMcpToolRegistry::Get();
	Registry.RegisterToolClass(UGenerateExternalContentTool::StaticClass());
	Registry.FindTool(TEXT("generate-external-content"));
}

void FUEBridgeMCPExternalAIModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCPExternalAI, Log, TEXT("UEBridgeMCPExternalAI module shutting down"));

	if (FModuleManager::Get().IsModuleLoaded(TEXT("UEBridgeMCP")))
	{
		FMcpToolRegistry::Get().UnregisterTool(TEXT("generate-external-content"));
	}
}

IMPLEMENT_MODULE(FUEBridgeMCPExternalAIModule, UEBridgeMCPExternalAI)
