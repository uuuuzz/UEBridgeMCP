// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPPCG.h"

#include "Tools/McpToolRegistry.h"
#include "Tools/PCG/GeneratePCGScatterTool.h"
#include "Tools/PCG/PCGGraphTools.h"

DEFINE_LOG_CATEGORY(LogUEBridgeMCPPCG);

void FUEBridgeMCPPCGModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPPCG, Log, TEXT("UEBridgeMCPPCG module starting up"));

	FMcpToolRegistry& Registry = FMcpToolRegistry::Get();
	Registry.RegisterToolClass(UGeneratePCGScatterTool::StaticClass());
	Registry.RegisterToolClass(UQueryPCGGraphSummaryTool::StaticClass());
	Registry.RegisterToolClass(UEditPCGGraphTool::StaticClass());
	Registry.RegisterToolClass(URunPCGGraphTool::StaticClass());
	Registry.RegisterToolAlias(TEXT("list_pcg_graphs"), TEXT("query-pcg-graph-summary"));
	Registry.RegisterToolAlias(TEXT("get_pcg_info"), TEXT("query-pcg-graph-summary"));
	Registry.RegisterToolAlias(TEXT("get_pcg_graph_nodes"), TEXT("query-pcg-graph-summary"));
	Registry.RegisterToolAlias(TEXT("create_pcg_graph"), TEXT("edit-pcg-graph"));
	Registry.RegisterToolAlias(TEXT("add_pcg_node"), TEXT("edit-pcg-graph"));
	Registry.RegisterToolAlias(TEXT("connect_pcg_nodes"), TEXT("edit-pcg-graph"));
	Registry.RegisterToolAlias(TEXT("set_pcg_static_mesh_spawner_meshes"), TEXT("edit-pcg-graph"));
	Registry.RegisterToolAlias(TEXT("execute_pcg"), TEXT("run-pcg-graph"));
	Registry.RegisterToolAlias(TEXT("spawn_pcg_actor"), TEXT("generate-pcg-scatter"));
	Registry.FindTool(TEXT("generate-pcg-scatter"));
	Registry.FindTool(TEXT("query-pcg-graph-summary"));
	Registry.FindTool(TEXT("edit-pcg-graph"));
	Registry.FindTool(TEXT("run-pcg-graph"));
}

void FUEBridgeMCPPCGModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCPPCG, Log, TEXT("UEBridgeMCPPCG module shutting down"));

	if (FModuleManager::Get().IsModuleLoaded(TEXT("UEBridgeMCP")))
	{
		FMcpToolRegistry::Get().UnregisterTool(TEXT("generate-pcg-scatter"));
		FMcpToolRegistry::Get().UnregisterTool(TEXT("query-pcg-graph-summary"));
		FMcpToolRegistry::Get().UnregisterTool(TEXT("edit-pcg-graph"));
		FMcpToolRegistry::Get().UnregisterTool(TEXT("run-pcg-graph"));
	}
}

IMPLEMENT_MODULE(FUEBridgeMCPPCGModule, UEBridgeMCPPCG)
