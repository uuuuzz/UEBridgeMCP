// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPEditor.h"
#include "Tools/McpToolRegistry.h"
#include "UI/McpToolbarExtension.h"

// v2 Query tools
#include "Tools/Blueprint/QueryBlueprintSummaryTool.h"
#include "Tools/Blueprint/QueryBlueprintGraphSummaryTool.h"
#include "Tools/Blueprint/QueryBlueprintNodeTool.h"
#include "Tools/Blueprint/QueryBlueprintFindingsTool.h"
#include "Tools/Blueprint/QueryBlueprintTool.h"
#include "Tools/Blueprint/QueryBlueprintGraphTool.h"
#include "Tools/Animation/QueryAnimationAssetSummaryTool.h"
#include "Tools/Animation/CreateAnimationMontageTool.h"
#include "Tools/Animation/EditAnimBlueprintStateMachineTool.h"
#include "Tools/Animation/AnimationAdvancedTools.h"
#include "Tools/Level/QueryLevelSummaryTool.h"
#include "Tools/Level/QueryLevelTool.h"
#include "Tools/Level/QueryActorDetailTool.h"
#include "Tools/Level/QueryActorSelectionTool.h"
#include "Tools/Level/QuerySpatialContextTool.h"
#include "Tools/Level/QueryWorldSummaryTool.h"
#include "Tools/Level/QueryWorldTool.h"
#include "Tools/Material/QueryMaterialSummaryTool.h"
#include "Tools/Material/QueryMaterialTool.h"
#include "Tools/Material/QueryMaterialInstanceTool.h"
#include "Tools/Material/CreateMaterialInstanceTool.h"
#include "Tools/Material/EditMaterialInstanceTool.h"
#include "Tools/Material/EditMaterialGraphTool.h"
#include "Tools/StaticMesh/QueryStaticMeshSummaryTool.h"
#include "Tools/StaticMesh/EditStaticMeshSettingsTool.h"
#include "Tools/StaticMesh/ReplaceStaticMeshTool.h"
#include "Tools/StaticMesh/StaticMeshAdvancedTools.h"
#include "Tools/Environment/QueryEnvironmentSummaryTool.h"
#include "Tools/Environment/EditEnvironmentLightingTool.h"
#include "Tools/Gameplay/CreateAIBehaviorAssetsTool.h"
#include "Tools/Gameplay/CreateAttributeSetTool.h"
#include "Tools/Gameplay/CreateGameFrameworkBlueprintSetTool.h"
#include "Tools/Gameplay/CreateGameplayAbilityTool.h"
#include "Tools/Gameplay/CreateGameplayEffectTool.h"
#include "Tools/Gameplay/CreateInputActionTool.h"
#include "Tools/Gameplay/CreateInputMappingContextTool.h"
#include "Tools/Gameplay/EditGameplayEffectModifiersTool.h"
#include "Tools/Gameplay/EditInputMappingContextTool.h"
#include "Tools/Gameplay/EditReplicationSettingsTool.h"
#include "Tools/Gameplay/ManageAbilitySystemBindingsTool.h"
#include "Tools/Gameplay/ManageGameplayTagsTool.h"
#include "Tools/Gameplay/QueryNavigationStateTool.h"
#include "Tools/Gameplay/QueryGASAssetSummaryTool.h"
#include "Tools/Gameplay/QueryReplicationSummaryTool.h"
#include "Tools/Gameplay/GameplayAdvancedTools.h"
#include "Tools/Project/ProjectInfoTool.h"
#include "Tools/Project/QueryWorkspaceHealthTool.h"
#include "Tools/Asset/QueryAssetTool.h"
#include "Tools/Asset/QueryDataTableTool.h"
#include "Tools/Asset/GetAssetDiffTool.h"
#include "Tools/Asset/EditDataTableBatchTool.h"
#include "Tools/Asset/CreateUserDefinedStructTool.h"
#include "Tools/Asset/CreateUserDefinedEnumTool.h"
#include "Tools/Asset/ManageAssetFoldersTool.h"
#include "Tools/Asset/QueryUnusedAssetsTool.h"
#include "Tools/Analysis/ClassHierarchyTool.h"
#include "Tools/Analysis/QueryClassMemberSummaryTool.h"
#include "Tools/Analysis/QueryEditorSubsystemSummaryTool.h"
#include "Tools/Analysis/QueryEngineApiSymbolTool.h"
#include "Tools/Analysis/QueryPluginCapabilitiesTool.h"
#include "Tools/References/FindReferencesTool.h"
#include "Tools/Widget/WidgetBlueprintTool.h"
#include "Tools/Widget/CreateWidgetBlueprintTool.h"
#include "Tools/Widget/EditWidgetBlueprintTool.h"
#include "Tools/Widget/EditWidgetLayoutBatchTool.h"
#include "Tools/Widget/EditWidgetAnimationTool.h"
#include "Tools/Widget/EditWidgetComponentTool.h"
#include "Tools/Widget/CommonUITools.h"
#include "Tools/Editor/EditorInteractionTools.h"
#include "Tools/Debug/GetLogsTool.h"
#include "Tools/Workflow/BuiltInWorkflowContent.h"
#include "Tools/Workflow/GenerateBlueprintPatternTool.h"
#include "Tools/Workflow/GenerateLevelPatternTool.h"
#include "Tools/Workflow/ManageWorkflowPresetsTool.h"
#include "Tools/Workflow/RunEditorMacroTool.h"
#include "Tools/Workflow/RunProjectMaintenanceChecksTool.h"
#include "Tools/Workflow/RunWorkflowPresetTool.h"
#include "Tools/Performance/QueryPerformanceReportTool.h"
#include "Tools/Performance/CapturePerformanceSnapshotTool.h"
#include "Tools/Performance/PerformanceDetailTools.h"
#include "Tools/Sequencer/EditSequencerTracksTool.h"
#include "Tools/Sequencer/QueryLevelSequenceSummaryTool.h"
#include "Tools/Landscape/CreateLandscapeTool.h"
#include "Tools/Landscape/EditLandscapeRegionTool.h"
#include "Tools/Landscape/QueryLandscapeSummaryTool.h"
#include "Tools/Foliage/EditFoliageBatchTool.h"
#include "Tools/Foliage/QueryFoliageSummaryTool.h"
#include "Tools/Niagara/ApplyNiagaraSystemToActorTool.h"
#include "Tools/Niagara/CreateNiagaraSystemFromTemplateTool.h"
#include "Tools/Niagara/EditNiagaraUserParametersTool.h"
#include "Tools/Niagara/QueryNiagaraEmitterSummaryTool.h"
#include "Tools/Niagara/QueryNiagaraSystemSummaryTool.h"
#include "Tools/Audio/ApplyAudioToActorTool.h"
#include "Tools/Audio/CreateAudioComponentSetupTool.h"
#include "Tools/Audio/CreateSoundCueTool.h"
#include "Tools/Audio/EditSoundCueRoutingTool.h"
#include "Tools/Audio/QueryAudioAssetSummaryTool.h"
#include "Tools/MetaSound/CreateMetaSoundSourceTool.h"
#include "Tools/MetaSound/EditMetaSoundGraphTool.h"
#include "Tools/MetaSound/QueryMetaSoundSummaryTool.h"
#include "Tools/MetaSound/SetMetaSoundInputDefaultsTool.h"
#include "Tools/Physics/ApplyPhysicalMaterialTool.h"
#include "Tools/Physics/CreatePhysicsConstraintTool.h"
#include "Tools/Physics/EditCollisionSettingsTool.h"
#include "Tools/Physics/EditPhysicsConstraintTool.h"
#include "Tools/Physics/EditPhysicsSimulationTool.h"
#include "Tools/Physics/QueryPhysicsSummaryTool.h"
#include "Tools/Search/SearchAssetsAdvancedTool.h"
#include "Tools/Search/SearchBlueprintSymbolsTool.h"
#include "Tools/Search/SearchContentByClassTool.h"
#include "Tools/Search/SearchLevelEntitiesTool.h"
#include "Tools/Search/SearchProjectTool.h"
#include "Tools/World/EditSplineActorsTool.h"
#include "Tools/World/QueryWorldPartitionCellsTool.h"
#include "Tools/World/WorldPartitionEditTools.h"
#include "Utils/McpOptionalCapabilityUtils.h"
#include "Dom/JsonObject.h"

#include <initializer_list>

// Utility write tools
#include "Tools/Write/CreateAssetTool.h"
#include "Tools/Write/AddComponentTool.h"
#include "Tools/Write/AddWidgetTool.h"
#include "Tools/Write/AddDataTableRowTool.h"
#include "Tools/Write/AddGraphNodeTool.h"
#include "Tools/Write/ConnectGraphPinsTool.h"
#include "Tools/Write/DisconnectGraphPinTool.h"
#include "Tools/Write/RemoveGraphNodeTool.h"
#include "Tools/Write/SetPropertyTool.h"
#include "Tools/Write/SpawnActorTool.h"

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
#include "Tools/PIE/QueryRuntimeActorStateTool.h"
#include "Tools/PIE/QueryAbilitySystemStateTool.h"
#include "Tools/PIE/TraceGameplayCollisionTool.h"

// Function calling
#include "Tools/CallFunctionTool.h"

// v2 batch editing + authoring
#include "Tools/Blueprint/EditBlueprintGraphTool.h"
#include "Tools/Blueprint/EditBlueprintMembersTool.h"
#include "Tools/Blueprint/EditBlueprintComponentsTool.h"
#include "Tools/Blueprint/CreateBlueprintFunctionTool.h"
#include "Tools/Blueprint/CreateBlueprintEventTool.h"
#include "Tools/Blueprint/EditBlueprintFunctionSignatureTool.h"
#include "Tools/Blueprint/ManageBlueprintInterfacesTool.h"
#include "Tools/Blueprint/LayoutBlueprintGraphTool.h"
#include "Tools/Blueprint/AnalyzeBlueprintCompileResultsTool.h"
#include "Tools/Blueprint/ApplyBlueprintFixupsTool.h"
#include "Tools/Blueprint/CreateBlueprintPatternTool.h"
#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Level/EditLevelActorTool.h"
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

namespace
{
	TSharedPtr<FJsonObject> CloneArguments(const TSharedPtr<FJsonObject>& Arguments)
	{
		TSharedPtr<FJsonObject> ClonedArguments = MakeShareable(new FJsonObject);
		if (Arguments.IsValid())
		{
			ClonedArguments->Values = Arguments->Values;
		}
		return ClonedArguments;
	}

	FString FirstStringField(const TSharedPtr<FJsonObject>& Arguments, std::initializer_list<const TCHAR*> FieldNames)
	{
		if (!Arguments.IsValid())
		{
			return FString();
		}

		for (const TCHAR* FieldName : FieldNames)
		{
			FString Value;
			if (Arguments->TryGetStringField(FieldName, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	TSharedPtr<FJsonObject> AdaptNetworkComponentArguments(const TSharedPtr<FJsonObject>& Arguments)
	{
		TSharedPtr<FJsonObject> AdaptedArguments = CloneArguments(Arguments);

		if (!AdaptedArguments->HasField(TEXT("component_name")))
		{
			const FString ComponentName = FirstStringField(Arguments, { TEXT("component_name"), TEXT("component"), TEXT("componentName") });
			if (!ComponentName.IsEmpty())
			{
				AdaptedArguments->SetStringField(TEXT("component_name"), ComponentName);
			}
		}

		if (!AdaptedArguments->HasField(TEXT("asset_path")))
		{
			const FString AssetPath = FirstStringField(Arguments, { TEXT("asset_path"), TEXT("blueprint_path"), TEXT("blueprint"), TEXT("path") });
			if (!AssetPath.IsEmpty())
			{
				AdaptedArguments->SetStringField(TEXT("asset_path"), AssetPath);
			}
		}

		if (!AdaptedArguments->HasField(TEXT("actor_name")))
		{
			const FString ActorName = FirstStringField(Arguments, { TEXT("actor_name"), TEXT("actor"), TEXT("actor_label") });
			if (!ActorName.IsEmpty())
			{
				AdaptedArguments->SetStringField(TEXT("actor_name"), ActorName);
			}
		}

		if (!AdaptedArguments->HasField(TEXT("target_type")))
		{
			AdaptedArguments->SetStringField(
				TEXT("target_type"),
				AdaptedArguments->HasField(TEXT("actor_name")) && !AdaptedArguments->HasField(TEXT("asset_path")) ? TEXT("actor") : TEXT("asset"));
		}

		return AdaptedArguments;
	}

	TSharedPtr<FJsonObject> AdaptSetComponentReplicationArguments(const TSharedPtr<FJsonObject>& Arguments)
	{
		TSharedPtr<FJsonObject> AdaptedArguments = AdaptNetworkComponentArguments(Arguments);
		if (!AdaptedArguments->HasField(TEXT("replicates")) && Arguments.IsValid())
		{
			bool bReplicates = false;
			if (Arguments->TryGetBoolField(TEXT("enabled"), bReplicates)
				|| Arguments->TryGetBoolField(TEXT("replicate"), bReplicates)
				|| Arguments->TryGetBoolField(TEXT("is_replicated"), bReplicates))
			{
				AdaptedArguments->SetBoolField(TEXT("replicates"), bReplicates);
			}
		}
		return AdaptedArguments;
	}

	void RegisterCompatibilityAliases(FMcpToolRegistry& Registry)
	{
		// Name-only aliases for common snake_case tool names.
		// The canonical UEBridgeMCP tool schema remains the source of truth.
		Registry.RegisterToolAlias(TEXT("get_project_info"), TEXT("get-project-info"));
		Registry.RegisterToolAlias(TEXT("execute_python"), TEXT("run-python-script"));
		Registry.RegisterToolAlias(TEXT("search_project"), TEXT("search-project"));
		Registry.RegisterToolAlias(TEXT("get_selection"), TEXT("query-actor-selection"));
		Registry.RegisterToolAlias(TEXT("select_actors"), TEXT("edit-editor-selection"));
		Registry.RegisterToolAlias(TEXT("set_viewport_camera"), TEXT("edit-viewport-camera"));
		Registry.RegisterToolAlias(TEXT("run_console_command"), TEXT("run-editor-command"));
		Registry.RegisterToolAlias(TEXT("take_screenshot"), TEXT("capture-viewport"));
		Registry.RegisterToolAlias(TEXT("get_render_stats"), TEXT("query-render-stats"));
		Registry.RegisterToolAlias(TEXT("get_memory_report"), TEXT("query-memory-report"));
		Registry.RegisterToolAlias(TEXT("profile_actors_in_view"), TEXT("profile-visible-actors"));

		Registry.RegisterToolAlias(TEXT("get_asset_info"), TEXT("query-asset"));
		Registry.RegisterToolAlias(TEXT("import_asset"), TEXT("import-assets"));
		Registry.RegisterToolAlias(TEXT("import_asset_with_settings"), TEXT("import-assets"));
		Registry.RegisterToolAlias(TEXT("get_asset_references"), TEXT("find-references"));
		Registry.RegisterToolAlias(TEXT("find_unused_assets"), TEXT("query-unused-assets"));
		Registry.RegisterToolAlias(TEXT("get_datatable_rows"), TEXT("query-datatable"));
		Registry.RegisterToolAlias(TEXT("add_datatable_row"), TEXT("add-datatable-row"));
		Registry.RegisterToolAlias(TEXT("create_user_struct"), TEXT("create-user-defined-struct"));
		Registry.RegisterToolAlias(TEXT("create_enum"), TEXT("create-user-defined-enum"));
		Registry.RegisterToolAlias(TEXT("create_folder"), TEXT("manage-asset-folders"));
		Registry.RegisterToolAlias(TEXT("move_assets_to_folder"), TEXT("manage-asset-folders"));
		Registry.RegisterToolAlias(TEXT("delete_asset"), TEXT("manage-assets"));
		Registry.RegisterToolAlias(TEXT("duplicate_asset"), TEXT("manage-assets"));
		Registry.RegisterToolAlias(TEXT("rename_asset"), TEXT("manage-assets"));

		Registry.RegisterToolAlias(TEXT("get_blueprint_info"), TEXT("query-blueprint-summary"));
		Registry.RegisterToolAlias(TEXT("compile_blueprint"), TEXT("compile-assets"));
		Registry.RegisterToolAlias(TEXT("validate_blueprint"), TEXT("analyze-blueprint-compile-results"));
		Registry.RegisterToolAlias(TEXT("add_component"), TEXT("add-component"));
		Registry.RegisterToolAlias(TEXT("set_component_property"), TEXT("edit-blueprint-components"));
		Registry.RegisterToolAlias(TEXT("connect_pins"), TEXT("connect-graph-pins"));
		Registry.RegisterToolAlias(TEXT("disconnect_pin"), TEXT("disconnect-graph-pin"));
		Registry.RegisterToolAlias(TEXT("remove_node"), TEXT("remove-graph-node"));
		Registry.RegisterToolAlias(TEXT("add_variable"), TEXT("edit-blueprint-members"));
		Registry.RegisterToolAlias(TEXT("add_local_variable"), TEXT("edit-blueprint-members"));
		Registry.RegisterToolAlias(TEXT("add_function_graph"), TEXT("create-blueprint-function"));
		Registry.RegisterToolAlias(TEXT("add_custom_event"), TEXT("create-blueprint-event"));
		Registry.RegisterToolAlias(TEXT("add_event_node"), TEXT("create-blueprint-event"));
		Registry.RegisterToolAlias(TEXT("add_interface_to_blueprint"), TEXT("manage-blueprint-interfaces"));
		Registry.RegisterToolAlias(TEXT("get_blueprint_interfaces"), TEXT("manage-blueprint-interfaces"));
		Registry.RegisterToolAlias(TEXT("get_function_signature"), TEXT("query-class-member-summary"));
		Registry.RegisterToolAlias(TEXT("get_node_pins"), TEXT("query-blueprint-node"));
		Registry.RegisterToolAlias(TEXT("add_branch_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_cast_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_delay_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_flow_control_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_sequence_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_timeline_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_variable_get_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("add_variable_set_node"), TEXT("edit-blueprint-graph"));
		Registry.RegisterToolAlias(TEXT("set_pin_default_value"), TEXT("set-property"));

		Registry.RegisterToolAlias(TEXT("get_level_info"), TEXT("query-level-summary"));
		Registry.RegisterToolAlias(TEXT("list_actors"), TEXT("search-level-entities"));
		Registry.RegisterToolAlias(TEXT("get_actor_properties"), TEXT("query-actor-detail"));
		Registry.RegisterToolAlias(TEXT("set_actor_property"), TEXT("edit-level-actor"));
		Registry.RegisterToolAlias(TEXT("set_actor_transform"), TEXT("edit-level-actor"));
		Registry.RegisterToolAlias(TEXT("set_actor_hidden"), TEXT("edit-level-actor"));
		Registry.RegisterToolAlias(TEXT("set_actor_mobility"), TEXT("edit-level-actor"));
		Registry.RegisterToolAlias(TEXT("set_actor_tags"), TEXT("edit-level-actor"));
		Registry.RegisterToolAlias(TEXT("create_actor"), TEXT("spawn-actor"));
		Registry.RegisterToolAlias(TEXT("align_actors"), TEXT("align-actors-batch"));
		Registry.RegisterToolAlias(TEXT("place_actor_on_ground"), TEXT("drop-actors-to-surface"));
		Registry.RegisterToolAlias(TEXT("get_spatial_context"), TEXT("query-spatial-context"));

		Registry.RegisterToolAlias(TEXT("get_static_mesh_info"), TEXT("query-static-mesh-summary"));
		Registry.RegisterToolAlias(TEXT("get_mesh_complexity_report"), TEXT("query-mesh-complexity"));
		Registry.RegisterToolAlias(TEXT("set_mesh_material_slots"), TEXT("edit-static-mesh-slots"));
		Registry.RegisterToolAlias(TEXT("configure_mesh_lod"), TEXT("edit-static-mesh-settings"));
		Registry.RegisterToolAlias(TEXT("enable_nanite"), TEXT("edit-static-mesh-settings"));
		Registry.RegisterToolAlias(TEXT("set_static_mesh"), TEXT("replace-static-mesh"));
		Registry.RegisterToolAlias(TEXT("assign_material"), TEXT("apply-material"));
		Registry.RegisterToolAlias(TEXT("create_material_instance"), TEXT("create-material-instance"));
		Registry.RegisterToolAlias(TEXT("set_material_scalar"), TEXT("edit-material-instance-batch"));
		Registry.RegisterToolAlias(TEXT("set_material_vector"), TEXT("edit-material-instance-batch"));
		Registry.RegisterToolAlias(TEXT("add_material_expression"), TEXT("edit-material-graph"));
		Registry.RegisterToolAlias(TEXT("add_texture_sample_expression"), TEXT("edit-material-graph"));
		Registry.RegisterToolAlias(TEXT("add_material_parameter_expression"), TEXT("edit-material-graph"));
		Registry.RegisterToolAlias(TEXT("connect_material_expression"), TEXT("edit-material-graph"));
		Registry.RegisterToolAlias(TEXT("remove_material_expression"), TEXT("edit-material-graph"));
		Registry.RegisterToolAlias(TEXT("set_material_expression_value"), TEXT("edit-material-graph"));
		Registry.RegisterToolAlias(TEXT("compile_material"), TEXT("compile-assets"));

		Registry.RegisterToolAlias(TEXT("set_post_process_settings"), TEXT("edit-environment-lighting"));
		Registry.RegisterToolAlias(TEXT("set_fog_settings"), TEXT("edit-environment-lighting"));
		Registry.RegisterToolAlias(TEXT("set_sky_atmosphere"), TEXT("edit-environment-lighting"));
		Registry.RegisterToolAlias(TEXT("set_light_properties"), TEXT("edit-environment-lighting"));

		Registry.RegisterToolAlias(TEXT("get_skeleton_info"), TEXT("query-skeleton-summary"));
		Registry.RegisterToolAlias(TEXT("get_anim_blueprint_info"), TEXT("query-animation-asset-summary"));
		Registry.RegisterToolAlias(TEXT("get_anim_montage_info"), TEXT("query-animation-asset-summary"));
		Registry.RegisterToolAlias(TEXT("create_anim_montage"), TEXT("create-animation-montage"));
		Registry.RegisterToolAlias(TEXT("create_blend_space"), TEXT("create-blend-space"));
		Registry.RegisterToolAlias(TEXT("add_blend_space_sample"), TEXT("edit-blend-space-samples"));
		Registry.RegisterToolAlias(TEXT("add_anim_notify"), TEXT("edit-animation-notifies"));
		Registry.RegisterToolAlias(TEXT("list_anim_notifies"), TEXT("edit-animation-notifies"));
		Registry.RegisterToolAlias(TEXT("create_anim_state_machine"), TEXT("edit-anim-blueprint-state-machine"));
		Registry.RegisterToolAlias(TEXT("add_anim_state"), TEXT("edit-anim-blueprint-state-machine"));
		Registry.RegisterToolAlias(TEXT("add_anim_transition"), TEXT("edit-anim-blueprint-state-machine"));
		Registry.RegisterToolAlias(TEXT("get_anim_state_machine_info"), TEXT("query-animation-asset-summary"));

		Registry.RegisterToolAlias(TEXT("create_input_action"), TEXT("create-input-action"));
		Registry.RegisterToolAlias(TEXT("create_input_mapping_context"), TEXT("create-input-mapping-context"));
		Registry.RegisterToolAlias(TEXT("add_action_mapping"), TEXT("edit-input-mapping-context"));
		Registry.RegisterToolAlias(TEXT("add_gameplay_tags"), TEXT("manage-gameplay-tags"));
		Registry.RegisterToolAlias(TEXT("list_gameplay_tags"), TEXT("manage-gameplay-tags"));
		Registry.RegisterToolAlias(TEXT("set_actor_gameplay_tags"), TEXT("manage-gameplay-tags"));
		Registry.RegisterToolAlias(TEXT("create_gameplay_ability"), TEXT("create-gameplay-ability"));
		Registry.RegisterToolAlias(TEXT("create_gameplay_effect"), TEXT("create-gameplay-effect"));
		Registry.RegisterToolAlias(TEXT("create_attribute_set"), TEXT("create-attribute-set"));
		Registry.RegisterToolAlias(TEXT("add_ability_component"), TEXT("manage-ability-system-bindings"));
		Registry.RegisterToolAlias(TEXT("get_gas_info"), TEXT("query-gas-asset-summary"));
		Registry.RegisterToolAlias(TEXT("list_gameplay_abilities"), TEXT("query-gas-asset-summary"));
		Registry.RegisterToolAlias(TEXT("list_gameplay_effects"), TEXT("query-gas-asset-summary"));
		Registry.RegisterToolAlias(TEXT("list_attribute_sets"), TEXT("query-gas-asset-summary"));
		Registry.RegisterToolAlias(TEXT("create_game_mode"), TEXT("create-gameframework-blueprint-set"));
		Registry.RegisterToolAlias(TEXT("create_player_controller"), TEXT("create-gameframework-blueprint-set"));
		Registry.RegisterToolAlias(TEXT("create_game_state"), TEXT("create-gameframework-blueprint-set"));
		Registry.RegisterToolAlias(TEXT("create_player_state"), TEXT("create-gameframework-blueprint-set"));
		Registry.RegisterToolAlias(TEXT("create_hud"), TEXT("create-gameframework-blueprint-set"));
		Registry.RegisterToolAlias(TEXT("get_game_framework_info"), TEXT("query-blueprint-summary"));

		Registry.RegisterToolAlias(TEXT("create_behavior_tree"), TEXT("create-ai-behavior-assets"));
		Registry.RegisterToolAlias(TEXT("create_blackboard"), TEXT("create-ai-behavior-assets"));
		Registry.RegisterToolAlias(TEXT("add_blackboard_key"), TEXT("edit-blackboard-keys"));
		Registry.RegisterToolAlias(TEXT("get_behavior_tree_info"), TEXT("query-ai-behavior-assets"));
		Registry.RegisterToolAlias(TEXT("get_blackboard_info"), TEXT("query-ai-behavior-assets"));
		Registry.RegisterToolAlias(TEXT("list_ai_assets"), TEXT("query-ai-behavior-assets"));
		Registry.RegisterToolAlias(TEXT("build_navigation"), TEXT("edit-navigation-build"));
		Registry.RegisterToolAlias(TEXT("query_navigation_path"), TEXT("query-navigation-path"));
		Registry.RegisterToolAlias(TEXT("get_navigation_info"), TEXT("query-navigation-state"));
		Registry.RegisterToolAlias(TEXT("get_replication_info"), TEXT("query-replication-summary"));
		Registry.RegisterToolAlias(TEXT("set_replication_settings"), TEXT("edit-replication-settings"));
		Registry.RegisterToolAlias(TEXT("get_component_replication"), TEXT("query-network-component-settings"));
		Registry.RegisterToolAliasArgumentAdapter(TEXT("get_component_replication"), AdaptNetworkComponentArguments);
		Registry.RegisterToolAlias(TEXT("set_component_replication"), TEXT("edit-network-component-settings"));
		Registry.RegisterToolAliasArgumentAdapter(TEXT("set_component_replication"), AdaptSetComponentReplicationArguments);
		Registry.RegisterToolAlias(TEXT("set_net_dormancy"), TEXT("edit-network-component-settings"));
		Registry.RegisterToolAliasArgumentAdapter(TEXT("set_net_dormancy"), AdaptNetworkComponentArguments);

		Registry.RegisterToolAlias(TEXT("get_physics_info"), TEXT("query-physics-summary"));
		Registry.RegisterToolAlias(TEXT("set_physics_simulation"), TEXT("edit-physics-simulation"));
		Registry.RegisterToolAlias(TEXT("set_collision_profile"), TEXT("edit-collision-settings"));
		Registry.RegisterToolAlias(TEXT("set_collision_response"), TEXT("edit-collision-settings"));
		Registry.RegisterToolAlias(TEXT("add_physics_constraint"), TEXT("create-physics-constraint"));
		Registry.RegisterToolAlias(TEXT("assign_physics_material"), TEXT("apply-physical-material"));
		Registry.RegisterToolAlias(TEXT("list_collision_channels"), TEXT("query-physics-summary"));

		Registry.RegisterToolAlias(TEXT("spawn_sound"), TEXT("apply-audio-to-actor"));
		Registry.RegisterToolAlias(TEXT("get_sound_info"), TEXT("query-audio-asset-summary"));
		Registry.RegisterToolAlias(TEXT("set_audio_properties"), TEXT("create-audio-component-setup"));
		Registry.RegisterToolAlias(TEXT("spawn_niagara_system"), TEXT("apply-niagara-system-to-actor"));
		Registry.RegisterToolAlias(TEXT("get_niagara_parameters"), TEXT("query-niagara-system-summary"));
		Registry.RegisterToolAlias(TEXT("set_niagara_parameter"), TEXT("edit-niagara-user-parameters"));
		Registry.RegisterToolAlias(TEXT("get_metasound_info"), TEXT("query-metasound-summary"));
		Registry.RegisterToolAlias(TEXT("create_metasound_source"), TEXT("create-metasound-source"));
		Registry.RegisterToolAlias(TEXT("set_metasound_parameter"), TEXT("set-metasound-input-defaults"));

		Registry.RegisterToolAlias(TEXT("create_level_sequence"), TEXT("create-asset"));
		Registry.RegisterToolAlias(TEXT("get_sequence_info"), TEXT("query-level-sequence-summary"));
		Registry.RegisterToolAlias(TEXT("set_sequence_range"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("add_sequence_track"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("add_actor_to_sequence"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("add_keyframe"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("play_sequence"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("open_sequence"), TEXT("query-level-sequence-summary"));
		Registry.RegisterToolAlias(TEXT("add_audio_track"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("add_camera_cut_track"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("add_sub_sequence"), TEXT("edit-sequencer-tracks"));
		Registry.RegisterToolAlias(TEXT("add_fade_track"), TEXT("edit-sequencer-tracks"));

		Registry.RegisterToolAlias(TEXT("create_landscape"), TEXT("create-landscape"));
		Registry.RegisterToolAlias(TEXT("get_landscape_info"), TEXT("query-landscape-summary"));
		Registry.RegisterToolAlias(TEXT("set_landscape_material"), TEXT("edit-landscape-region"));
		Registry.RegisterToolAlias(TEXT("paint_foliage"), TEXT("edit-foliage-batch"));
		Registry.RegisterToolAlias(TEXT("erase_foliage"), TEXT("edit-foliage-batch"));
		Registry.RegisterToolAlias(TEXT("get_foliage_stats"), TEXT("query-foliage-summary"));
		Registry.RegisterToolAlias(TEXT("add_foliage_type"), TEXT("edit-foliage-batch"));
		Registry.RegisterToolAlias(TEXT("get_world_partition_info"), TEXT("query-worldpartition-cells"));
		Registry.RegisterToolAlias(TEXT("load_world_partition_region"), TEXT("edit-worldpartition-cells"));

		Registry.RegisterToolAlias(TEXT("create_spline_actor"), TEXT("edit-spline-actors"));
		Registry.RegisterToolAlias(TEXT("add_spline_point"), TEXT("edit-spline-actors"));
		Registry.RegisterToolAlias(TEXT("set_spline_point"), TEXT("edit-spline-actors"));
		Registry.RegisterToolAlias(TEXT("remove_spline_point"), TEXT("edit-spline-actors"));
		Registry.RegisterToolAlias(TEXT("get_spline_info"), TEXT("edit-spline-actors"));
		Registry.RegisterToolAlias(TEXT("set_spline_closed"), TEXT("edit-spline-actors"));
		Registry.RegisterToolAlias(TEXT("set_spline_type"), TEXT("edit-spline-actors"));

		Registry.RegisterToolAlias(TEXT("create_state_tree"), TEXT("add-statetree-state"));
		Registry.RegisterToolAlias(TEXT("get_state_tree_info"), TEXT("query-statetree"));
		Registry.RegisterToolAlias(TEXT("add_state_tree_state"), TEXT("add-statetree-state"));
		Registry.RegisterToolAlias(TEXT("set_state_tree_evaluator"), TEXT("edit-statetree-bindings"));
		Registry.RegisterToolAlias(TEXT("list_state_trees"), TEXT("query-statetree"));

		Registry.RegisterToolAlias(TEXT("create_widget_blueprint"), TEXT("create-widget-blueprint"));
		Registry.RegisterToolAlias(TEXT("get_widget_tree"), TEXT("inspect-widget-blueprint"));
		Registry.RegisterToolAlias(TEXT("get_widget_properties"), TEXT("inspect-widget-blueprint"));
		Registry.RegisterToolAlias(TEXT("add_widget"), TEXT("add-widget"));
		Registry.RegisterToolAlias(TEXT("remove_widget"), TEXT("edit-widget-layout-batch"));
		Registry.RegisterToolAlias(TEXT("move_widget"), TEXT("edit-widget-layout-batch"));
		Registry.RegisterToolAlias(TEXT("set_widget_slot"), TEXT("edit-widget-layout-batch"));
		Registry.RegisterToolAlias(TEXT("set_widget_properties"), TEXT("edit-widget-blueprint"));
		Registry.RegisterToolAlias(TEXT("set_widget_image"), TEXT("edit-widget-blueprint"));
		Registry.RegisterToolAlias(TEXT("batch_add_widgets"), TEXT("edit-widget-layout-batch"));
		Registry.RegisterToolAlias(TEXT("batch_set_widget_properties"), TEXT("edit-widget-layout-batch"));
		Registry.RegisterToolAlias(TEXT("spawn_widget_component"), TEXT("edit-widget-component"));
		Registry.RegisterToolAlias(TEXT("set_widget_component_property"), TEXT("edit-widget-component"));
		Registry.RegisterToolAlias(TEXT("bind_widget_event"), TEXT("edit-widget-blueprint"));
		Registry.RegisterToolAlias(TEXT("create_common_ui_widget"), TEXT("create-common-ui-widget"));
		Registry.RegisterToolAlias(TEXT("configure_common_button"), TEXT("edit-common-ui"));
		Registry.RegisterToolAlias(TEXT("set_common_ui_input_mode"), TEXT("edit-common-ui"));
		Registry.RegisterToolAlias(TEXT("list_common_ui_widgets"), TEXT("query-common-ui-widgets"));

		Registry.RegisterToolAlias(TEXT("create_basic_level"), TEXT("generate-level-pattern"));
		Registry.RegisterToolAlias(TEXT("create_grid_layout"), TEXT("generate-level-pattern"));
		Registry.RegisterToolAlias(TEXT("create_light_rig"), TEXT("generate-level-pattern"));
		Registry.RegisterToolAlias(TEXT("create_ring_layout"), TEXT("generate-level-pattern"));
		Registry.RegisterToolAlias(TEXT("create_staircase"), TEXT("generate-level-pattern"));
		Registry.RegisterToolAlias(TEXT("create_trigger_volume"), TEXT("generate-level-pattern"));
		Registry.RegisterToolAlias(TEXT("create_scene_from_template"), TEXT("generate-level-structure"));
	}
}

void FUEBridgeMCPEditorModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("UEBridgeMCPEditor module starting up"));

	RegisterBuiltInTools();
	BuiltInWorkflowContent::RegisterBuiltInResourcesAndPrompts();

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
	Registry.RegisterToolClass(UQueryBlueprintFindingsTool::StaticClass());
	Registry.RegisterToolClass(UQueryBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UQueryBlueprintGraphTool::StaticClass());
	Registry.RegisterToolClass(UQueryAnimationAssetSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQuerySkeletonSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryLevelSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryLevelTool::StaticClass());
	Registry.RegisterToolClass(UQueryActorDetailTool::StaticClass());
	Registry.RegisterToolClass(UQueryActorSelectionTool::StaticClass());
	Registry.RegisterToolClass(UQuerySpatialContextTool::StaticClass());
	Registry.RegisterToolClass(UQueryWorldSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryWorldTool::StaticClass());
	Registry.RegisterToolClass(UQueryMaterialSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryMaterialTool::StaticClass());
	Registry.RegisterToolClass(UQueryMaterialInstanceTool::StaticClass());
	Registry.RegisterToolClass(UQueryStaticMeshSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryMeshComplexityTool::StaticClass());
	Registry.RegisterToolClass(UQueryEnvironmentSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryNavigationStateTool::StaticClass());
	Registry.RegisterToolClass(UQueryNavigationPathTool::StaticClass());
	Registry.RegisterToolClass(UQueryReplicationSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryPhysicsSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryGASAssetSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryAIBehaviorAssetsTool::StaticClass());

	// Project / asset / utility queries
	Registry.RegisterToolClass(UProjectInfoTool::StaticClass());
	Registry.RegisterToolClass(UQueryWorkspaceHealthTool::StaticClass());
	Registry.RegisterToolClass(UQueryAssetTool::StaticClass());
	Registry.RegisterToolClass(UQueryDataTableTool::StaticClass());
	Registry.RegisterToolClass(UGetAssetDiffTool::StaticClass());
	Registry.RegisterToolClass(UQueryUnusedAssetsTool::StaticClass());
	Registry.RegisterToolClass(UClassHierarchyTool::StaticClass());
	Registry.RegisterToolClass(UQueryEngineApiSymbolTool::StaticClass());
	Registry.RegisterToolClass(UQueryClassMemberSummaryTool::StaticClass());
	Registry.RegisterToolClass(UQueryPluginCapabilitiesTool::StaticClass());
	Registry.RegisterToolClass(UQueryEditorSubsystemSummaryTool::StaticClass());
	Registry.RegisterToolClass(UFindReferencesTool::StaticClass());
	Registry.RegisterToolClass(USearchProjectTool::StaticClass());
	Registry.RegisterToolClass(USearchAssetsAdvancedTool::StaticClass());
	Registry.RegisterToolClass(USearchBlueprintSymbolsTool::StaticClass());
	Registry.RegisterToolClass(USearchLevelEntitiesTool::StaticClass());
	Registry.RegisterToolClass(USearchContentByClassTool::StaticClass());
	Registry.RegisterToolClass(UWidgetBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UCreateWidgetBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetBlueprintTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetLayoutBatchTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetAnimationTool::StaticClass());
	Registry.RegisterToolClass(UEditWidgetComponentTool::StaticClass());
	Registry.RegisterToolClass(UCreateCommonUIWidgetTool::StaticClass());
	Registry.RegisterToolClass(UEditCommonUITool::StaticClass());
	Registry.RegisterToolClass(UQueryCommonUIWidgetsTool::StaticClass());
	Registry.RegisterToolClass(UGetLogsTool::StaticClass());
	Registry.RegisterToolClass(UEditEditorSelectionTool::StaticClass());
	Registry.RegisterToolClass(UEditViewportCameraTool::StaticClass());
	Registry.RegisterToolClass(URunEditorCommandTool::StaticClass());
	Registry.RegisterToolClass(UManageWorkflowPresetsTool::StaticClass());
	Registry.RegisterToolClass(URunWorkflowPresetTool::StaticClass());
	Registry.RegisterToolClass(URunEditorMacroTool::StaticClass());
	Registry.RegisterToolClass(URunProjectMaintenanceChecksTool::StaticClass());
	Registry.RegisterToolClass(UGenerateBlueprintPatternTool::StaticClass());
	Registry.RegisterToolClass(UGenerateLevelPatternTool::StaticClass());
	Registry.RegisterToolClass(UQueryPerformanceReportTool::StaticClass());
	Registry.RegisterToolClass(UCapturePerformanceSnapshotTool::StaticClass());
	Registry.RegisterToolClass(UQueryRenderStatsTool::StaticClass());
	Registry.RegisterToolClass(UQueryMemoryReportTool::StaticClass());
	Registry.RegisterToolClass(UProfileVisibleActorsTool::StaticClass());
	if (FMcpOptionalCapabilityUtils::IsSequencerAvailable())
	{
		Registry.RegisterToolClass(UQueryLevelSequenceSummaryTool::StaticClass());
		Registry.RegisterToolClass(UEditSequencerTracksTool::StaticClass());
	}
	if (FMcpOptionalCapabilityUtils::IsLandscapeAvailable())
	{
		Registry.RegisterToolClass(UQueryLandscapeSummaryTool::StaticClass());
		Registry.RegisterToolClass(UCreateLandscapeTool::StaticClass());
		Registry.RegisterToolClass(UEditLandscapeRegionTool::StaticClass());
	}
	if (FMcpOptionalCapabilityUtils::IsFoliageAvailable())
	{
		Registry.RegisterToolClass(UQueryFoliageSummaryTool::StaticClass());
		Registry.RegisterToolClass(UEditFoliageBatchTool::StaticClass());
	}
	if (FMcpOptionalCapabilityUtils::IsWorldPartitionAvailable())
	{
		Registry.RegisterToolClass(UQueryWorldPartitionCellsTool::StaticClass());
		Registry.RegisterToolClass(UEditWorldPartitionCellsTool::StaticClass());
	}
	if (FMcpOptionalCapabilityUtils::IsNiagaraAvailable())
	{
		Registry.RegisterToolClass(UQueryNiagaraSystemSummaryTool::StaticClass());
		Registry.RegisterToolClass(UQueryNiagaraEmitterSummaryTool::StaticClass());
		Registry.RegisterToolClass(UCreateNiagaraSystemFromTemplateTool::StaticClass());
		Registry.RegisterToolClass(UEditNiagaraUserParametersTool::StaticClass());
		Registry.RegisterToolClass(UApplyNiagaraSystemToActorTool::StaticClass());
	}
	Registry.RegisterToolClass(UQueryAudioAssetSummaryTool::StaticClass());
	Registry.RegisterToolClass(UCreateSoundCueTool::StaticClass());
	Registry.RegisterToolClass(UEditSoundCueRoutingTool::StaticClass());
	Registry.RegisterToolClass(UCreateAudioComponentSetupTool::StaticClass());
	Registry.RegisterToolClass(UApplyAudioToActorTool::StaticClass());
	if (FMcpOptionalCapabilityUtils::IsMetaSoundAvailable())
	{
		Registry.RegisterToolClass(UQueryMetaSoundSummaryTool::StaticClass());
		Registry.RegisterToolClass(UCreateMetaSoundSourceTool::StaticClass());
		Registry.RegisterToolClass(UEditMetaSoundGraphTool::StaticClass());
		Registry.RegisterToolClass(USetMetaSoundInputDefaultsTool::StaticClass());
	}

	// Utility creation tools
	Registry.RegisterToolClass(UCreateAssetTool::StaticClass());
	Registry.RegisterToolClass(UAddComponentTool::StaticClass());
	Registry.RegisterToolClass(UAddWidgetTool::StaticClass());
	Registry.RegisterToolClass(UAddDataTableRowTool::StaticClass());
	Registry.RegisterToolClass(UAddGraphNodeTool::StaticClass());
	Registry.RegisterToolClass(UConnectGraphPinsTool::StaticClass());
	Registry.RegisterToolClass(UDisconnectGraphPinTool::StaticClass());
	Registry.RegisterToolClass(URemoveGraphNodeTool::StaticClass());
	Registry.RegisterToolClass(USetPropertyTool::StaticClass());
	Registry.RegisterToolClass(USpawnActorTool::StaticClass());

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
	Registry.RegisterToolClass(UQueryRuntimeActorStateTool::StaticClass());
	Registry.RegisterToolClass(UQueryAbilitySystemStateTool::StaticClass());
	Registry.RegisterToolClass(UTraceGameplayCollisionTool::StaticClass());

	// Function calling tool
	Registry.RegisterToolClass(UCallFunctionTool::StaticClass());

	// === v2 Blueprint / Level / Material batch tools ===
	Registry.RegisterToolClass(UEditBlueprintGraphTool::StaticClass());
	Registry.RegisterToolClass(UEditBlueprintMembersTool::StaticClass());
	Registry.RegisterToolClass(UEditBlueprintComponentsTool::StaticClass());
	Registry.RegisterToolClass(UCreateBlueprintFunctionTool::StaticClass());
	Registry.RegisterToolClass(UCreateBlueprintEventTool::StaticClass());
	Registry.RegisterToolClass(UEditBlueprintFunctionSignatureTool::StaticClass());
	Registry.RegisterToolClass(UManageBlueprintInterfacesTool::StaticClass());
	Registry.RegisterToolClass(ULayoutBlueprintGraphTool::StaticClass());
	Registry.RegisterToolClass(UAnalyzeBlueprintCompileResultsTool::StaticClass());
	Registry.RegisterToolClass(UApplyBlueprintFixupsTool::StaticClass());
	Registry.RegisterToolClass(UCreateBlueprintPatternTool::StaticClass());
	Registry.RegisterToolClass(UEditDataTableBatchTool::StaticClass());
	Registry.RegisterToolClass(UEditLevelActorTool::StaticClass());
	Registry.RegisterToolClass(UEditLevelBatchTool::StaticClass());
	Registry.RegisterToolClass(UAlignActorsBatchTool::StaticClass());
	Registry.RegisterToolClass(UDropActorsToSurfaceTool::StaticClass());
	Registry.RegisterToolClass(UEditStaticMeshSettingsTool::StaticClass());
	Registry.RegisterToolClass(UReplaceStaticMeshTool::StaticClass());
	Registry.RegisterToolClass(UEditStaticMeshSlotsTool::StaticClass());
	Registry.RegisterToolClass(UEditMaterialInstanceBatchTool::StaticClass());
	Registry.RegisterToolClass(UEditMaterialInstanceTool::StaticClass());
	Registry.RegisterToolClass(UCreateMaterialInstanceTool::StaticClass());
	Registry.RegisterToolClass(UEditMaterialGraphTool::StaticClass());
	Registry.RegisterToolClass(UEditEnvironmentLightingTool::StaticClass());
	Registry.RegisterToolClass(UCreateAnimationMontageTool::StaticClass());
	Registry.RegisterToolClass(UEditAnimBlueprintStateMachineTool::StaticClass());
	Registry.RegisterToolClass(UCreateBlendSpaceTool::StaticClass());
	Registry.RegisterToolClass(UEditBlendSpaceSamplesTool::StaticClass());
	Registry.RegisterToolClass(UEditAnimationNotifiesTool::StaticClass());
	Registry.RegisterToolClass(UEditAnimGraphNodeTool::StaticClass());
	Registry.RegisterToolClass(UCreateInputActionTool::StaticClass());
	Registry.RegisterToolClass(UCreateInputMappingContextTool::StaticClass());
	Registry.RegisterToolClass(UEditInputMappingContextTool::StaticClass());
	Registry.RegisterToolClass(UManageGameplayTagsTool::StaticClass());
	Registry.RegisterToolClass(UCreateGameplayAbilityTool::StaticClass());
	Registry.RegisterToolClass(UCreateGameplayEffectTool::StaticClass());
	Registry.RegisterToolClass(UCreateAttributeSetTool::StaticClass());
	Registry.RegisterToolClass(UEditGameplayEffectModifiersTool::StaticClass());
	Registry.RegisterToolClass(UManageAbilitySystemBindingsTool::StaticClass());
	Registry.RegisterToolClass(UCreateGameFrameworkBlueprintSetTool::StaticClass());
	Registry.RegisterToolClass(UCreateAIBehaviorAssetsTool::StaticClass());
	Registry.RegisterToolClass(UEditBlackboardKeysTool::StaticClass());
	Registry.RegisterToolClass(UEditReplicationSettingsTool::StaticClass());
	Registry.RegisterToolClass(UQueryNetworkComponentSettingsTool::StaticClass());
	Registry.RegisterToolClass(UEditNetworkComponentSettingsTool::StaticClass());
	Registry.RegisterToolClass(UEditNavigationBuildTool::StaticClass());
	Registry.RegisterToolClass(UEditCollisionSettingsTool::StaticClass());
	Registry.RegisterToolClass(UEditPhysicsSimulationTool::StaticClass());
	Registry.RegisterToolClass(UCreatePhysicsConstraintTool::StaticClass());
	Registry.RegisterToolClass(UEditPhysicsConstraintTool::StaticClass());
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
	Registry.RegisterToolClass(UApplyPhysicalMaterialTool::StaticClass());
	Registry.RegisterToolClass(UEditSplineActorsTool::StaticClass());

	// === P2 - High-Level Orchestration ===
	Registry.RegisterToolClass(UBlueprintScaffoldFromSpecTool::StaticClass());
	Registry.RegisterToolClass(UQueryGameplayStateTool::StaticClass());
	Registry.RegisterToolClass(UAutoFixBlueprintCompileErrorsTool::StaticClass());
	Registry.RegisterToolClass(UGenerateLevelStructureTool::StaticClass());
	RegisterCompatibilityAliases(Registry);

	// P0-H2: 模块加载阶段（GameThread）预实例化所有工具，避免首次 NewObject/AddToRoot
	// 发生在后台线程（当工具 RequiresGameThread()==false 时，ExecuteTool 会在后台线程调用 FindTool）。
	Registry.WarmupAllTools();

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("Registered %d MCP tools"), Registry.GetToolCount());
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEBridgeMCPEditorModule, UEBridgeMCPEditor)
