// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCP.h"
#include "Misc/EngineVersionComparison.h"

// Enforce minimum engine version at compile time
static_assert(UE_VERSION_NEWER_THAN(5, 5, 0), "ue-bridge-mcp requires Unreal Engine 5.6 or higher. Please upgrade your engine version.");

DEFINE_LOG_CATEGORY(LogUEBridgeMCP);

#define LOCTEXT_NAMESPACE "FUEBridgeMCPModule"

void FUEBridgeMCPModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCP, Log, TEXT("UEBridgeMCP module starting up"));
}

void FUEBridgeMCPModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCP, Log, TEXT("UEBridgeMCP module shutting down"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEBridgeMCPModule, UEBridgeMCP)
