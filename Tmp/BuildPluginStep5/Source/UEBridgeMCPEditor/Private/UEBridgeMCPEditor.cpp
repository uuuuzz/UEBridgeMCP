// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPEditor.h"
#include "Tools/McpToolRegistry.h"
#include "UI/McpToolbarExtension.h"

// v2 Query tools
#include "Tools/Blueprint/QueryBlueprintSummaryTool.h"
#include "Tools/Blueprint/QueryBlueprintGraphSummaryTool.h"
#include "Tools/Blueprint/QueryBlueprintNodeTool.h"
#include "Tools/Level/QueryLevelSummaryTool.h"
#include "Tools/Level/QueryActorDetailTool.h"
#include "Tools/Level/QueryActorSelectionTool.h"
#include "Tools/Level/QuerySpatialContextTool.h"
#include "Tools/Level/QueryWorldSummaryTool.h"
#include "Tools/Material/QueryMaterialSummaryTool.h"
#include "Tools/Material/QueryMaterialInstanceTool.h"
#include "Tools/Material/CreateMaterialInstanceTool.h"
#include "Tools/Material/EditMaterialGraphTool.h"
#include "Tools/StaticMesh/QueryStaticMeshSummaryTool.h"
#include "Tools/StaticMesh/EditStaticMeshSettingsTool.h"
#include "Tools/StaticMesh/ReplaceStaticMeshTool.h"
#include "Tools/Environment/QueryEnvironmentSummaryTool.h"
#include "Tools/Environment/EditEnvironmentLightingTool.h"
#include "Tools/Gameplay/CreateAIBehaviorAssetsTool.h"
#include "Tools/Gameplay/CreateGameFrameworkBlueprintSetTool.h"
#include "Tools/Gameplay/CreateInputActionTool.h"
#include "Tools/Gameplay/CreateInputMappingContextTool.h"
#include "Tools/Gameplay/EditInputMappingContextTool.h"
#include "Tools/Gameplay/EditReplicationSettingsTool.h"
#include "Tools/Gameplay/ManageGameplayTagsTool.h"
#include "Tools/Gameplay/QueryNavigationStateTool.h"
#include "Tools/Gameplay/QueryReplicationSummaryTool.h"
#include "Tools/Project/ProjectInfoTool.h"
#include "Tools/Asset/QueryAssetTool.h"
#include "Tools/Asset/QueryDataTableTool.h"
#include "Tools/Asset/GetAssetDiffTool.h"
#include "Tools/Asset/EditDataTableBatchTool.h"
#include "Tools/Asset/CreateUserDefinedStructTool.h"
#include "Tools/Asset/CreateUserDefinedEnumTool.h"
#include "Tools/Asset/ManageAssetFoldersTool.h"
#include "Tools/Asset/QueryUnusedAssetsTool.h"
#include "Tools/Analysis/ClassHierarchyTool.h"
#include "Tools/References/FindReferencesTool.h"
#include "Tools/Widget/WidgetBlueprintTool.h"
#include "Tools/Widget/CreateWidgetBlueprintTool.h"
#include "Tools/Widget/EditWidgetBlueprintTool.h"
#include "Tools/Widget/EditWidgetLayoutBatchTool.h"
#include "Tools/Widget/EditWidgetAnimationTool.h"
#include "Tools/Widget/EditWidgetComponentTool.h"
#include "Tools/Debug/GetLogsTool.h"

// Utility write tools
#include "Tools/Write/CreateAssetTool.h"
#include "Tools/Write/AddWidgetTool.h"
#include "Tools/Write/AddDataTableRowTool.h"

// StateTree tools
#include "Tools/StateTree/QueryStateTreeTool.h"
#include "Tools/StateTree/AddStateTreeStateTool.h"
#include "Tools/StateTree/AddStateTreeTransitionTool.h"
#include "Tools/StateTree/AddStateTreeTaskTool.h"
#include "Tools/StateTree/EditStateTreeBindingsTool.h"
#include "Tools/StateTree/RemoveStateTreeStateTool.h"

// Scripting tools
#include "Tools/Scripting/RunPythonScriptTool.h"

// Build tools
#include "Tools/Build/TriggerLiveCodingTool.h"
#include "Tools/Build/BuildAndRelaunchTool.h"

// PIE (Play-In-Editor) tools
#include "Tools/PIE/PieSessionTool.h"
#include "Tools/PIE/PieInputTool.h"
#include "Tools/PIE/AssertWorldStateTool.h"
#include "Tools/PIE/WaitForWorldConditionTool.h"

// Function calling
#include "Tools/CallFunctionTool.h"

// v2 batch editing + authoring
#include "Tools/Blueprint/EditBlueprintGraphTool.h"
#include "Tools/Blueprint/EditBlueprintMembersTool.h"
#include "Tools/Blueprint/EditBlueprintComponentsTool.h"
#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Level/EditLevelBatchTool.h"
#include "Tools/Level/AlignActorsBatchTool.h"
#include "Tools/Level/DropActorsToSurfaceTool.h"

// P1 - Asset lifecycle
#include "Tools/Asset/ManageAssetsTool.h"
#include "Tools/Asset/ImportAssetsTool.h"
#include "Tools/Asset/SourceControlAssetsTool.h"
#include "Tools/Debug/CaptureViewportTool.h"
#include "Tools/Material/EditMaterialInstanceBatchTool.h"
#include "Tools/Material/ApplyMaterialTool.h"

// P2 - High-level orchestration
#include "Tools/Blueprint/BlueprintScaffoldFromSpecTool.h"
#include "Tools/PIE/QueryGameplayStateTool.h"
#include "Tools/Blueprint/AutoFixBlueprintCompileErrorsTool.h"
#include "Tools/Level/GenerateLevelStructureTool.h"

DEFINE_LOG_CATEGORY(LogUEBridgeMCPEditor);
DEFINE_LOG_CATEGORY(LogUEBridgeMCP);

#define LOCTEXT_NAMESPACE "FUEBridgeMCPEditorModule"

void FUEBridgeMCPEditorModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("UEBridgeMCPEditor module starting up"));

	RegisterBuiltInTools();

	// Initialize toolbar status icon
	FMcpToolbarExtension::Initialize();
}

void FUEBridgeMCPEditorModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("UEBridgeMCPEditor module shutting down"));

	// Cleanup toolbar extension
	FMcpToolbarExtension::Shutdown();
}

void FUEBridgeMCPEditorModule::RegisterBuiltInTools()
{
	FMcpToolRegistry& Registry = FMcpToolRegistry::Get();

	// === v2 Query tools ===
	Registry.RegisterToolClass(UQueryBlueprintSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryBlueprintGraphSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryBlueprintNodeTool::StaticClass());
	Registry.RegisterToolClass(UQueryLevelSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryActorDetailTool::StaticClass());
	Registry.RegisterToolClass(UQueryActorSelectionTool::StaticClass());
	Registry.RegisterToolClass(UQuerySpatialContextTool::StaticClass());
	Registry.RegisterToolClass(UQueryWorldSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryMaterialSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryMaterialInstanceTool::StaticClass());
	Registry.RegisterToolClass(UQueryStaticMeshSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryEnvironmentSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryNavigationStateTool::StaticClass());
	Registry.RegisterToolClass(UQueryReplicationSummaryTool::StaticClass());

	// Project / asset / utility queries
	Registry.RegisterToolClass(UProjectInfoTool::StaticClass());
	Registry.RegisterToolClass(UQueryAssetTool::StaticClass());
	Registry.RegisterToolClass(UQueryDataTableTool::StaticClass());
	Registry.RegisterToolClass(UGetAssetDiffTool::StaticClass());
	Registry.RegisterToolClass(UQueryUnusedAssetsTool::StaticClass());
	Registry.RegisterToolClass(UClassHierarchyTool::StaticClass());
	Registry.RegisterToolClass(UFindReferencesTool::StaticClass());
	Registry.RegisterToolClass(UWidgetBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UCreateWidgetBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetLayoutBatchTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetAnimationTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetComponentTool::StaticClass());
	Registry.RegisterToolClass(UGetLogsTool::StaticClass());

	// Utility creation tools
	Registry.RegisterToolClass(UCreateAssetTool::StaticClass());
	Registry.RegisterToolClass(UAddWidgetTool::StaticClass());
	Registry.RegisterToolClass(UAddDataTableRowTool::StaticClass());

	// StateTree tools
	Registry.RegisterToolClass(UQueryStateTreeTool::StaticClass());
	Registry.RegisterToolClass(UAddStateTreeStateTool::StaticClass());
	Registry.RegisterToolClass(UAddStateTreeTransitionTool::StaticClass());
	Registry.RegisterToolClass(UAddStateTreeTaskTool::StaticClass());
	Registry.RegisterToolClass(UEditStateTreeBindingsTool::StaticClass());
	Registry.RegisterToolClass(URemoveStateTreeStateTool::StaticClass());

	// Scripting tools
	Registry.RegisterToolClass(URunPythonScriptTool::StaticClass());

	// Build tools
	Registry.RegisterToolClass(UTriggerLiveCodingTool::StaticClass());
	Registry.RegisterToolClass(UBuildAndRelaunchTool::StaticClass());

	// PIE (Play-In-Editor) tools
	Registry.RegisterToolClass(UPieSessionTool::StaticClass());
	Registry.RegisterToolClass(UPieInputTool::StaticClass());
	Registry.RegisterToolClass(UWaitForWorldConditionTool::StaticClass());
	Registry.RegisterToolClass(UAssertWorldStateTool::StaticClass());

	// Function calling tool
	Registry.RegisterToolClass(UCallFunctionTool::StaticClass());

	// === v2 Blueprint / Level / Material batch tools ===
	Registry.RegisterToolClass(UEditBlueprintGraphTool::StaticClass());
	Registry.RegisterToolClass(UEditBlueprintMembersTool::StaticClass());
	Registry.RegisterToolClass(UEditBlueprintComponentsTool::StaticClass());
	Registry.RegisterToolClass(UEditDataTableBatchTool::StaticClass());
	Registry.RegisterToolClass(UEditLevelBatchTool::StaticClass());
	Registry.RegisterToolClass(UAlignActorsBatchTool::StaticClass());
	Registry.RegisterToolClass(UDropActorsToSurfaceTool::StaticClass());
	Registry.RegisterToolClass(UEditStaticMeshSettingsTool::StaticClass());
	Registry.RegisterToolClass(UReplaceStaticMeshTool::StaticClass());
	Registry.RegisterToolClass(UEditMaterialInstanceBatchTool::StaticClass());
	Registry.RegisterToolClass(UCreateMaterialInstanceTool::StaticClass());
	Registry.RegisterToolClass(UEditMaterialGraphTool::StaticClass());
	Registry.RegisterToolClass(UEditEnvironmentLightingTool::StaticClass());
	Registry.RegisterToolClass(UCreateInputActionTool::StaticClass());
	Registry.RegisterToolClass(UCreateInputMappingContextTool::StaticClass());
	Registry.RegisterToolClass(UEditInputMappingContextTool::StaticClass());
	Registry.RegisterToolClass(UManageGameplayTagsTool::StaticClass());
	Registry.RegisterToolClass(UCreateGameFrameworkBlueprintSetTool::StaticClass());
	Registry.RegisterToolClass(UCreateAIBehaviorAssetsTool::StaticClass());
	Registry.RegisterToolClass(UEditReplicationSettingsTool::StaticClass());
	Registry.RegisterToolClass(UCompileAssetsTool::StaticClass());

	// === P1 - Asset Lifecycle and Validation ===
	Registry.RegisterToolClass(UManageAssetsTool::StaticClass());
	Registry.RegisterToolClass(UManageAssetFoldersTool::StaticClass());
	Registry.RegisterToolClass(UImportAssetsTool::StaticClass());
	Registry.RegisterToolClass(USourceControlAssetsTool::StaticClass());
	Registry.RegisterToolClass(UCreateUserDefinedStructTool::StaticClass());
	Registry.RegisterToolClass(UCreateUserDefinedEnumTool::StaticClass());
	Registry.RegisterToolClass(UCaptureViewportTool::StaticClass());
	Registry.RegisterToolClass(UApplyMaterialTool::StaticClass());

	// === P2 - High-Level Orchestration ===
	Registry.RegisterToolClass(UBlueprintScaffoldFromSpecTool::StaticClass());
	Registry.RegisterToolClass(UQueryGameplayStateTool::StaticClass());
	Registry.RegisterToolClass(UAutoFixBlueprintCompileErrorsTool::StaticClass());
	Registry.RegisterToolClass(UGenerateLevelStructureTool::StaticClass());

	// P0-H2: 模块加载阶段（GameThread）预实例化所有工具，避免首次 NewObject/AddToRoot
	// 发生在后台线程（当工具 RequiresGameThread()==false 时，ExecuteTool 会在后台线程调用 FindTool）。
	Registry.WarmupAllTools();

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("Registered %d MCP tools"), Registry.GetToolCount());
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEBridgeMCPEditorModule, UEBridgeMCPEditor)
