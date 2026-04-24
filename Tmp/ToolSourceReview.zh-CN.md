# UEBridgeMCP 逐工具源码审查报告

生成时间：2026-04-24 09:20:56

## 范围

- 识别到工具类：190
- 已注册工具类：190
- 源码存在但未注册：0
- 检查维度：注册可见性、工具元数据、schema/required、dry_run、rollback/save、旧式返回、直接 LoadObject、兼容别名风险。
- 注：逐工具表的“疑似写入/保存”来自对应类函数体的静态扫描；多工具共享 helper 的场景仍需结合人工复核。

## 本轮修复后状态

1. 未注册工具已归零；原 edit-material-instance/query-blueprint/query-blueprint-graph/query-level/query-material/query-world 已注册。
2. ExternalAI 旧别名增加参数适配；get_component_replication 改为只读 query-network-component-settings。
3. PCG/WorldPartition/edit-material-instance 批处理失败默认不保存部分修改，并补充 dry_run 校验。

## 逐工具清单


### ControlRig

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-control-rig-graph` | `UEditControlRigGraphTool` | True | `Source\UEBridgeMCPControlRig\Private\Tools\ControlRig\EditControlRigGraphTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |

### Analysis

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `get-class-hierarchy` | `UClassHierarchyTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Analysis\ClassHierarchyTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |
| `query-class-member-summary` | `UQueryClassMemberSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Analysis\QueryClassMemberSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_function_signature |
| `query-editor-subsystem-summary` | `UQueryEditorSubsystemSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Analysis\QueryEditorSubsystemSummaryTool.cpp` | 声明查询/未声明写入 |
| `query-engine-api-symbol` | `UQueryEngineApiSymbolTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Analysis\QueryEngineApiSymbolTool.cpp` | 声明查询/未声明写入 |
| `query-plugin-capabilities` | `UQueryPluginCapabilitiesTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Analysis\QueryPluginCapabilitiesTool.cpp` | 声明查询/未声明写入 |

### Animation

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `create-animation-montage` | `UCreateAnimationMontageTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\CreateAnimationMontageTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_anim_montage |
| `create-blend-space` | `UCreateBlendSpaceTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\AnimationAdvancedTools.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_blend_space |
| `edit-animation-notifies` | `UEditAnimationNotifiesTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\AnimationAdvancedTools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_anim_notify, list_anim_notifies；批处理失败/保存语义需复核 |
| `edit-anim-blueprint-state-machine` | `UEditAnimBlueprintStateMachineTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\EditAnimBlueprintStateMachineTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_anim_state_machine, add_anim_state, add_anim_transition；批处理失败/保存语义需复核 |
| `edit-anim-graph-node` | `UEditAnimGraphNodeTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\AnimationAdvancedTools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |
| `edit-blend-space-samples` | `UEditBlendSpaceSamplesTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\AnimationAdvancedTools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_blend_space_sample；批处理失败/保存语义需复核 |
| `query-animation-asset-summary` | `UQueryAnimationAssetSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\QueryAnimationAssetSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_anim_blueprint_info, get_anim_montage_info, get_anim_state_machine_info |
| `query-skeleton-summary` | `UQuerySkeletonSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Animation\AnimationAdvancedTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_skeleton_info |

### Asset

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `compile-assets` | `UCompileAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\CompileAssetsTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；存在保存逻辑但元数据未声明 SupportsSave；注册兼容别名，需做旧参数 smoke：compile_blueprint, compile_material |
| `create-user-defined-enum` | `UCreateUserDefinedEnumTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\CreateUserDefinedEnumTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_enum |
| `create-user-defined-struct` | `UCreateUserDefinedStructTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\CreateUserDefinedStructTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_user_struct |
| `edit-datatable-batch` | `UEditDataTableBatchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\EditDataTableBatchTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |
| `get-asset-diff` | `UGetAssetDiffTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\GetAssetDiffTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |
| `import-assets` | `UImportAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\ImportAssetsTool.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun；注册兼容别名，需做旧参数 smoke：import_asset, import_asset_with_settings |
| `manage-asset-folders` | `UManageAssetFoldersTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\ManageAssetFoldersTool.cpp` | 声明写入；声明批处理；声明 dry_run；注册兼容别名，需做旧参数 smoke：create_folder, move_assets_to_folder；批处理失败/保存语义需复核 |
| `manage-assets` | `UManageAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\ManageAssetsTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave；注册兼容别名，需做旧参数 smoke：delete_asset, duplicate_asset, rename_asset |
| `query-asset` | `UQueryAssetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\QueryAssetTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：get_asset_info |
| `query-datatable` | `UQueryDataTableTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\QueryDataTableTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_datatable_rows |
| `query-unused-assets` | `UQueryUnusedAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\QueryUnusedAssetsTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：find_unused_assets |
| `source-control-assets` | `USourceControlAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Asset\SourceControlAssetsTool.cpp` | 声明查询/未声明写入 |

### Audio

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `apply-audio-to-actor` | `UApplyAudioToActorTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Audio\ApplyAudioToActorTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：spawn_sound |
| `create-audio-component-setup` | `UCreateAudioComponentSetupTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Audio\CreateAudioComponentSetupTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_audio_properties |
| `create-sound-cue` | `UCreateSoundCueTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Audio\CreateSoundCueTool.cpp` | 声明写入；声明 save |
| `edit-sound-cue-routing` | `UEditSoundCueRoutingTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Audio\EditSoundCueRoutingTool.cpp` | 声明写入；声明 dry_run；声明 save |
| `query-audio-asset-summary` | `UQueryAudioAssetSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Audio\QueryAudioAssetSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_sound_info |

### Blueprint

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `analyze-blueprint-compile-results` | `UAnalyzeBlueprintCompileResultsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\AnalyzeBlueprintCompileResultsTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：validate_blueprint |
| `apply-blueprint-fixups` | `UApplyBlueprintFixupsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\ApplyBlueprintFixupsTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave |
| `auto-fix-blueprint-compile-errors` | `UAutoFixBlueprintCompileErrorsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\AutoFixBlueprintCompileErrorsTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave |
| `blueprint-scaffold-from-spec` | `UBlueprintScaffoldFromSpecTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\BlueprintScaffoldFromSpecTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave |
| `create-blueprint-event` | `UCreateBlueprintEventTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\CreateBlueprintEventTool.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun；注册兼容别名，需做旧参数 smoke：add_custom_event, add_event_node |
| `create-blueprint-function` | `UCreateBlueprintFunctionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\CreateBlueprintFunctionTool.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun；注册兼容别名，需做旧参数 smoke：add_function_graph |
| `create-blueprint-pattern` | `UCreateBlueprintPatternTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\CreateBlueprintPatternTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；存在保存逻辑但元数据未声明 SupportsSave |
| `edit-blueprint-components` | `UEditBlueprintComponentsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\EditBlueprintComponentsTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：set_component_property |
| `edit-blueprint-function-signature` | `UEditBlueprintFunctionSignatureTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\EditBlueprintFunctionSignatureTool.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun |
| `edit-blueprint-graph` | `UEditBlueprintGraphTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\EditBlueprintGraphTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_branch_node, add_cast_node, add_delay_node, add_flow_control_node, add_sequence_node, add_timeline_node, add_variable_get_node, add_variable_set_node；批处理失败/保存语义需复核 |
| `edit-blueprint-members` | `UEditBlueprintMembersTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\EditBlueprintMembersTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave；注册兼容别名，需做旧参数 smoke：add_variable, add_local_variable |
| `layout-blueprint-graph` | `ULayoutBlueprintGraphTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\LayoutBlueprintGraphTool.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun |
| `manage-blueprint-interfaces` | `UManageBlueprintInterfacesTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\ManageBlueprintInterfacesTool.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun；注册兼容别名，需做旧参数 smoke：add_interface_to_blueprint, get_blueprint_interfaces |
| `query-blueprint` | `UQueryBlueprintTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\QueryBlueprintTool.cpp` | 声明查询/未声明写入 |
| `query-blueprint-findings` | `UQueryBlueprintFindingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\QueryBlueprintFindingsTool.cpp` | 声明查询/未声明写入 |
| `query-blueprint-graph` | `UQueryBlueprintGraphTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\QueryBlueprintGraphTool.cpp` | 声明查询/未声明写入 |
| `query-blueprint-graph-summary` | `UQueryBlueprintGraphSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\QueryBlueprintGraphSummaryTool.cpp` | 声明查询/未声明写入 |
| `query-blueprint-node` | `UQueryBlueprintNodeTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\QueryBlueprintNodeTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_node_pins |
| `query-blueprint-summary` | `UQueryBlueprintSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Blueprint\QueryBlueprintSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_blueprint_info, get_game_framework_info |

### Build

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `build-and-relaunch` | `UBuildAndRelaunchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Build\BuildAndRelaunchTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |
| `trigger-live-coding` | `UTriggerLiveCodingTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Build\TriggerLiveCodingTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |

### CallFunctionTool.h

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `call-function` | `UCallFunctionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\CallFunctionTool.cpp` | 声明查询/未声明写入 |

### Debug

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `capture-viewport` | `UCaptureViewportTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Debug\CaptureViewportTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：take_screenshot |
| `get-logs` | `UGetLogsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Debug\GetLogsTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |

### Editor

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-editor-selection` | `UEditEditorSelectionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Editor\EditorInteractionTools.cpp` | 声明写入；声明 dry_run；注册兼容别名，需做旧参数 smoke：select_actors |
| `edit-viewport-camera` | `UEditViewportCameraTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Editor\EditorInteractionTools.cpp` | 声明写入；声明 dry_run；注册兼容别名，需做旧参数 smoke：set_viewport_camera |
| `run-editor-command` | `URunEditorCommandTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Editor\EditorInteractionTools.cpp` | 声明写入；声明 dry_run；注册兼容别名，需做旧参数 smoke：run_console_command |

### Environment

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-environment-lighting` | `UEditEnvironmentLightingTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Environment\EditEnvironmentLightingTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_post_process_settings, set_fog_settings, set_sky_atmosphere, set_light_properties；批处理失败/保存语义需复核 |
| `query-environment-summary` | `UQueryEnvironmentSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Environment\QueryEnvironmentSummaryTool.cpp` | 声明查询/未声明写入 |

### Foliage

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-foliage-batch` | `UEditFoliageBatchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Foliage\EditFoliageBatchTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：paint_foliage, erase_foliage, add_foliage_type；批处理失败/保存语义需复核 |
| `query-foliage-summary` | `UQueryFoliageSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Foliage\QueryFoliageSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_foliage_stats |

### Gameplay

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `create-ai-behavior-assets` | `UCreateAIBehaviorAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateAIBehaviorAssetsTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_behavior_tree, create_blackboard |
| `create-attribute-set` | `UCreateAttributeSetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateAttributeSetTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_attribute_set |
| `create-gameframework-blueprint-set` | `UCreateGameFrameworkBlueprintSetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateGameFrameworkBlueprintSetTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_game_mode, create_player_controller, create_game_state, create_player_state, create_hud |
| `create-gameplay-ability` | `UCreateGameplayAbilityTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateGameplayAbilityTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_gameplay_ability |
| `create-gameplay-effect` | `UCreateGameplayEffectTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateGameplayEffectTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_gameplay_effect |
| `create-input-action` | `UCreateInputActionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateInputActionTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_input_action |
| `create-input-mapping-context` | `UCreateInputMappingContextTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\CreateInputMappingContextTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_input_mapping_context |
| `edit-blackboard-keys` | `UEditBlackboardKeysTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\GameplayAdvancedTools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_blackboard_key；批处理失败/保存语义需复核 |
| `edit-gameplay-effect-modifiers` | `UEditGameplayEffectModifiersTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\EditGameplayEffectModifiersTool.cpp` | 声明写入；声明 dry_run；声明 save |
| `edit-input-mapping-context` | `UEditInputMappingContextTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\EditInputMappingContextTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_action_mapping；批处理失败/保存语义需复核 |
| `edit-navigation-build` | `UEditNavigationBuildTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\GameplayAdvancedTools.cpp` | 声明写入；声明 dry_run；注册兼容别名，需做旧参数 smoke：build_navigation |
| `edit-network-component-settings` | `UEditNetworkComponentSettingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\GameplayAdvancedTools.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_component_replication, set_net_dormancy |
| `edit-replication-settings` | `UEditReplicationSettingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\EditReplicationSettingsTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_replication_settings；批处理失败/保存语义需复核 |
| `manage-ability-system-bindings` | `UManageAbilitySystemBindingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\ManageAbilitySystemBindingsTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_ability_component |
| `manage-gameplay-tags` | `UManageGameplayTagsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\ManageGameplayTagsTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_gameplay_tags, list_gameplay_tags, set_actor_gameplay_tags；批处理失败/保存语义需复核 |
| `query-ai-behavior-assets` | `UQueryAIBehaviorAssetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\GameplayAdvancedTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_behavior_tree_info, get_blackboard_info, list_ai_assets |
| `query-gas-asset-summary` | `UQueryGASAssetSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\QueryGASAssetSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_gas_info, list_gameplay_abilities, list_gameplay_effects, list_attribute_sets |
| `query-navigation-path` | `UQueryNavigationPathTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\GameplayAdvancedTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：query_navigation_path |
| `query-navigation-state` | `UQueryNavigationStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\QueryNavigationStateTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_navigation_info |
| `query-network-component-settings` | `UQueryNetworkComponentSettingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\GameplayAdvancedTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_component_replication |
| `query-replication-summary` | `UQueryReplicationSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Gameplay\QueryReplicationSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_replication_info |

### Landscape

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `create-landscape` | `UCreateLandscapeTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Landscape\CreateLandscapeTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_landscape |
| `edit-landscape-region` | `UEditLandscapeRegionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Landscape\EditLandscapeRegionTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_landscape_material；批处理失败/保存语义需复核 |
| `query-landscape-summary` | `UQueryLandscapeSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Landscape\QueryLandscapeSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_landscape_info |

### Level

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `align-actors-batch` | `UAlignActorsBatchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\AlignActorsBatchTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：align_actors；批处理失败/保存语义需复核 |
| `drop-actors-to-surface` | `UDropActorsToSurfaceTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\DropActorsToSurfaceTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：place_actor_on_ground；批处理失败/保存语义需复核 |
| `edit-level-actor` | `UEditLevelActorTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\EditLevelActorTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：set_actor_property, set_actor_transform, set_actor_hidden, set_actor_mobility, set_actor_tags |
| `edit-level-batch` | `UEditLevelBatchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\EditLevelBatchTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |
| `generate-level-structure` | `UGenerateLevelStructureTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\GenerateLevelStructureTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；实现含 dry_run 但元数据未声明 SupportsDryRun；存在保存逻辑但元数据未声明 SupportsSave；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：create_scene_from_template |
| `query-actor-detail` | `UQueryActorDetailTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QueryActorDetailTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_actor_properties |
| `query-actor-selection` | `UQueryActorSelectionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QueryActorSelectionTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_selection |
| `query-level` | `UQueryLevelTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QueryLevelTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |
| `query-level-summary` | `UQueryLevelSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QueryLevelSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_level_info |
| `query-spatial-context` | `UQuerySpatialContextTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QuerySpatialContextTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_spatial_context |
| `query-world` | `UQueryWorldTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QueryWorldTool.cpp` | 声明查询/未声明写入 |
| `query-world-summary` | `UQueryWorldSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Level\QueryWorldSummaryTool.cpp` | 声明查询/未声明写入 |

### Material

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `apply-material` | `UApplyMaterialTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\ApplyMaterialTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；存在保存逻辑但元数据未声明 SupportsSave；注册兼容别名，需做旧参数 smoke：assign_material |
| `create-material-instance` | `UCreateMaterialInstanceTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\CreateMaterialInstanceTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_material_instance |
| `edit-material-graph` | `UEditMaterialGraphTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\EditMaterialGraphTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_material_expression, add_texture_sample_expression, add_material_parameter_expression, connect_material_expression, remove_material_expression, set_material_expression_value；批处理失败/保存语义需复核 |
| `edit-material-instance` | `UEditMaterialInstanceTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\EditMaterialInstanceTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |
| `edit-material-instance-batch` | `UEditMaterialInstanceBatchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\EditMaterialInstanceBatchTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_material_scalar, set_material_vector；批处理失败/保存语义需复核 |
| `query-material` | `UQueryMaterialTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\QueryMaterialTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一 |
| `query-material-instance` | `UQueryMaterialInstanceTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\QueryMaterialInstanceTool.cpp` | 声明查询/未声明写入 |
| `query-material-summary` | `UQueryMaterialSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Material\QueryMaterialSummaryTool.cpp` | 声明查询/未声明写入 |

### MetaSound

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `create-metasound-source` | `UCreateMetaSoundSourceTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\MetaSound\CreateMetaSoundSourceTool.cpp` | 声明写入；声明 save；注册兼容别名，需做旧参数 smoke：create_metasound_source |
| `edit-metasound-graph` | `UEditMetaSoundGraphTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\MetaSound\EditMetaSoundGraphTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |
| `query-metasound-summary` | `UQueryMetaSoundSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\MetaSound\QueryMetaSoundSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_metasound_info |
| `set-metasound-input-defaults` | `USetMetaSoundInputDefaultsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\MetaSound\SetMetaSoundInputDefaultsTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_metasound_parameter；批处理失败/保存语义需复核 |

### Niagara

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `apply-niagara-system-to-actor` | `UApplyNiagaraSystemToActorTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Niagara\ApplyNiagaraSystemToActorTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：spawn_niagara_system |
| `create-niagara-system-from-template` | `UCreateNiagaraSystemFromTemplateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Niagara\CreateNiagaraSystemFromTemplateTool.cpp` | 声明写入；声明 save |
| `edit-niagara-user-parameters` | `UEditNiagaraUserParametersTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Niagara\EditNiagaraUserParametersTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_niagara_parameter；批处理失败/保存语义需复核 |
| `query-niagara-emitter-summary` | `UQueryNiagaraEmitterSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Niagara\QueryNiagaraEmitterSummaryTool.cpp` | 声明查询/未声明写入 |
| `query-niagara-system-summary` | `UQueryNiagaraSystemSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Niagara\QueryNiagaraSystemSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_niagara_parameters |

### Performance

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `capture-performance-snapshot` | `UCapturePerformanceSnapshotTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Performance\CapturePerformanceSnapshotTool.cpp` | 声明查询/未声明写入 |
| `profile-visible-actors` | `UProfileVisibleActorsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Performance\PerformanceDetailTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：profile_actors_in_view |
| `query-memory-report` | `UQueryMemoryReportTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Performance\PerformanceDetailTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_memory_report |
| `query-performance-report` | `UQueryPerformanceReportTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Performance\QueryPerformanceReportTool.cpp` | 声明查询/未声明写入 |
| `query-render-stats` | `UQueryRenderStatsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Performance\PerformanceDetailTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_render_stats |

### Physics

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `apply-physical-material` | `UApplyPhysicalMaterialTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Physics\ApplyPhysicalMaterialTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：assign_physics_material |
| `create-physics-constraint` | `UCreatePhysicsConstraintTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Physics\CreatePhysicsConstraintTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：add_physics_constraint |
| `edit-collision-settings` | `UEditCollisionSettingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Physics\EditCollisionSettingsTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_collision_profile, set_collision_response |
| `edit-physics-constraint` | `UEditPhysicsConstraintTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Physics\EditPhysicsConstraintTool.cpp` | 声明写入；声明 dry_run；声明 save |
| `edit-physics-simulation` | `UEditPhysicsSimulationTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Physics\EditPhysicsSimulationTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_physics_simulation |
| `query-physics-summary` | `UQueryPhysicsSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Physics\QueryPhysicsSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_physics_info, list_collision_channels |

### PIE

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `assert-world-state` | `UAssertWorldStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\AssertWorldStateTool.cpp` | 声明查询/未声明写入 |
| `pie-input` | `UPieInputTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\PieInputTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |
| `pie-session` | `UPieSessionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\PieSessionTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回 |
| `query-ability-system-state` | `UQueryAbilitySystemStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\QueryAbilitySystemStateTool.cpp` | 声明查询/未声明写入 |
| `query-gameplay-state` | `UQueryGameplayStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\QueryGameplayStateTool.cpp` | 声明查询/未声明写入 |
| `query-runtime-actor-state` | `UQueryRuntimeActorStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\QueryRuntimeActorStateTool.cpp` | 声明查询/未声明写入 |
| `trace-gameplay-collision` | `UTraceGameplayCollisionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\TraceGameplayCollisionTool.cpp` | 声明查询/未声明写入 |
| `wait-for-world-condition` | `UWaitForWorldConditionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\PIE\WaitForWorldConditionTool.cpp` | 声明查询/未声明写入 |

### Project

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `get-project-info` | `UProjectInfoTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Project\ProjectInfoTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：get_project_info |
| `query-workspace-health` | `UQueryWorkspaceHealthTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Project\QueryWorkspaceHealthTool.cpp` | 声明查询/未声明写入 |

### References

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `find-references` | `UFindReferencesTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\References\FindReferencesTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：get_asset_references |

### Scripting

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `run-python-script` | `URunPythonScriptTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Scripting\RunPythonScriptTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：execute_python |

### Search

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `search-assets-advanced` | `USearchAssetsAdvancedTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Search\SearchAssetsAdvancedTool.cpp` | 声明查询/未声明写入 |
| `search-blueprint-symbols` | `USearchBlueprintSymbolsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Search\SearchBlueprintSymbolsTool.cpp` | 声明查询/未声明写入 |
| `search-content-by-class` | `USearchContentByClassTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Search\SearchContentByClassTool.cpp` | 声明查询/未声明写入 |
| `search-level-entities` | `USearchLevelEntitiesTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Search\SearchLevelEntitiesTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：list_actors |
| `search-project` | `USearchProjectTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Search\SearchProjectTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：search_project |

### Sequencer

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-sequencer-tracks` | `UEditSequencerTracksTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Sequencer\EditSequencerTracksTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_sequence_range, add_sequence_track, add_actor_to_sequence, add_keyframe, play_sequence, add_audio_track, add_camera_cut_track, add_sub_sequence, add_fade_track；批处理失败/保存语义需复核 |
| `query-level-sequence-summary` | `UQueryLevelSequenceSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Sequencer\QueryLevelSequenceSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_sequence_info, open_sequence |

### StateTree

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `add-statetree-state` | `UAddStateTreeStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StateTree\AddStateTreeStateTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：create_state_tree, add_state_tree_state |
| `add-statetree-task` | `UAddStateTreeTaskTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StateTree\AddStateTreeTaskTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一 |
| `add-statetree-transition` | `UAddStateTreeTransitionTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StateTree\AddStateTreeTransitionTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一 |
| `edit-statetree-bindings` | `UEditStateTreeBindingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StateTree\EditStateTreeBindingsTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_state_tree_evaluator；批处理失败/保存语义需复核 |
| `query-statetree` | `UQueryStateTreeTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StateTree\QueryStateTreeTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：get_state_tree_info, list_state_trees |
| `remove-statetree-state` | `URemoveStateTreeStateTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StateTree\RemoveStateTreeStateTool.cpp` | 声明查询/未声明写入；实现疑似写入但元数据未声明 MutatesState/GetToolKind；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一 |

### StaticMesh

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-static-mesh-settings` | `UEditStaticMeshSettingsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StaticMesh\EditStaticMeshSettingsTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：configure_mesh_lod, enable_nanite；批处理失败/保存语义需复核 |
| `edit-static-mesh-slots` | `UEditStaticMeshSlotsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StaticMesh\StaticMeshAdvancedTools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_mesh_material_slots；批处理失败/保存语义需复核 |
| `query-mesh-complexity` | `UQueryMeshComplexityTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StaticMesh\StaticMeshAdvancedTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_mesh_complexity_report |
| `query-static-mesh-summary` | `UQueryStaticMeshSummaryTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StaticMesh\QueryStaticMeshSummaryTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_static_mesh_info |
| `replace-static-mesh` | `UReplaceStaticMeshTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\StaticMesh\ReplaceStaticMeshTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_static_mesh；批处理失败/保存语义需复核 |

### Widget

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `create-common-ui-widget` | `UCreateCommonUIWidgetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\CommonUITools.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_common_ui_widget |
| `create-widget-blueprint` | `UCreateWidgetBlueprintTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\CreateWidgetBlueprintTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_widget_blueprint |
| `edit-common-ui` | `UEditCommonUITool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\CommonUITools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：configure_common_button, set_common_ui_input_mode；批处理失败/保存语义需复核 |
| `edit-widget-animation` | `UEditWidgetAnimationTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\EditWidgetAnimationTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；批处理失败/保存语义需复核 |
| `edit-widget-blueprint` | `UEditWidgetBlueprintTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\EditWidgetBlueprintTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：set_widget_properties, set_widget_image, bind_widget_event；批处理失败/保存语义需复核 |
| `edit-widget-component` | `UEditWidgetComponentTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\EditWidgetComponentTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：spawn_widget_component, set_widget_component_property；批处理失败/保存语义需复核 |
| `edit-widget-layout-batch` | `UEditWidgetLayoutBatchTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\EditWidgetLayoutBatchTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：remove_widget, move_widget, set_widget_slot, batch_add_widgets, batch_set_widget_properties；批处理失败/保存语义需复核 |
| `inspect-widget-blueprint` | `UWidgetBlueprintTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\WidgetBlueprintTool.cpp` | 声明查询/未声明写入；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：get_widget_tree, get_widget_properties |
| `query-common-ui-widgets` | `UQueryCommonUIWidgetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Widget\CommonUITools.cpp` | 声明查询/未声明写入；实现含 dry_run 但元数据未声明 SupportsDryRun；注册兼容别名，需做旧参数 smoke：list_common_ui_widgets |

### Workflow

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `generate-blueprint-pattern` | `UGenerateBlueprintPatternTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Workflow\GenerateBlueprintPatternTool.cpp` | 声明写入；声明 dry_run；声明 save |
| `generate-level-pattern` | `UGenerateLevelPatternTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Workflow\GenerateLevelPatternTool.cpp` | 声明写入；声明 dry_run；声明 save；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：create_basic_level, create_grid_layout, create_light_rig, create_ring_layout, create_staircase, create_trigger_volume |
| `manage-workflow-presets` | `UManageWorkflowPresetsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Workflow\ManageWorkflowPresetsTool.cpp` | 声明写入；声明批处理；声明 dry_run |
| `run-editor-macro` | `URunEditorMacroTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Workflow\RunEditorMacroTool.cpp` | 声明写入；声明 dry_run |
| `run-project-maintenance-checks` | `URunProjectMaintenanceChecksTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Workflow\RunProjectMaintenanceChecksTool.cpp` | 声明查询/未声明写入；声明 dry_run |
| `run-workflow-preset` | `URunWorkflowPresetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Workflow\RunWorkflowPresetTool.cpp` | 声明写入；声明 dry_run |

### World

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-spline-actors` | `UEditSplineActorsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\World\EditSplineActorsTool.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_spline_actor, add_spline_point, set_spline_point, remove_spline_point, get_spline_info, set_spline_closed, set_spline_type；批处理失败/保存语义需复核 |
| `edit-worldpartition-cells` | `UEditWorldPartitionCellsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\World\WorldPartitionEditTools.cpp` | 声明写入；声明批处理；声明 dry_run；注册兼容别名，需做旧参数 smoke：load_world_partition_region |
| `query-worldpartition-cells` | `UQueryWorldPartitionCellsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\World\QueryWorldPartitionCellsTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：get_world_partition_info |

### Write

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `add-component` | `UAddComponentTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\AddComponentTool.cpp` | 声明写入；注册兼容别名，需做旧参数 smoke：add_component |
| `add-datatable-row` | `UAddDataTableRowTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\AddDataTableRowTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：add_datatable_row |
| `add-graph-node` | `UAddGraphNodeTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\AddGraphNodeTool.cpp` | 声明写入；仍有旧式 Error/Json 返回 |
| `add-widget` | `UAddWidgetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\AddWidgetTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：add_widget |
| `connect-graph-pins` | `UConnectGraphPinsTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\ConnectGraphPinsTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：connect_pins |
| `create-asset` | `UCreateAssetTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\CreateAssetTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；存在直接 LoadObject，路径校验/错误码不完全统一；注册兼容别名，需做旧参数 smoke：create_level_sequence |
| `disconnect-graph-pin` | `UDisconnectGraphPinTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\DisconnectGraphPinTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：disconnect_pin |
| `remove-graph-node` | `URemoveGraphNodeTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\RemoveGraphNodeTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：remove_node |
| `set-property` | `USetPropertyTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\SetPropertyTool.cpp` | 声明写入；仍有旧式 Error/Json 返回；注册兼容别名，需做旧参数 smoke：set_pin_default_value |
| `spawn-actor` | `USpawnActorTool` | True | `Source\UEBridgeMCPEditor\Private\Tools\Write\SpawnActorTool.cpp` | 声明写入；注册兼容别名，需做旧参数 smoke：create_actor |

### ExternalAI

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `generate-external-asset` | `UGenerateExternalAssetTool` | True | `Source\UEBridgeMCPExternalAI\Private\Tools\ExternalAI\GenerateExternalAssetTool.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：generate_ui_image, remove_background, generate_3d_model, image_to_3d_model |
| `generate-external-content` | `UGenerateExternalContentTool` | True | `Source\UEBridgeMCPExternalAI\Private\Tools\ExternalAI\GenerateExternalContentTool.cpp` | 声明查询/未声明写入 |

### PCG

| 工具 | 类 | 注册 | 源码 | 审查结论 |
|---|---|---:|---|---|
| `edit-pcg-graph` | `UEditPCGGraphTool` | True | `Source\UEBridgeMCPPCG\Private\Tools\PCG\PCGGraphTools.cpp` | 声明写入；声明批处理；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：create_pcg_graph, add_pcg_node, connect_pcg_nodes, set_pcg_static_mesh_spawner_meshes；批处理失败/保存语义需复核 |
| `generate-pcg-scatter` | `UGeneratePCGScatterTool` | True | `Source\UEBridgeMCPPCG\Private\Tools\PCG\GeneratePCGScatterTool.cpp` | 声明写入；声明 dry_run；声明 save；注册兼容别名，需做旧参数 smoke：spawn_pcg_actor |
| `query-pcg-graph-summary` | `UQueryPCGGraphSummaryTool` | True | `Source\UEBridgeMCPPCG\Private\Tools\PCG\PCGGraphTools.cpp` | 声明查询/未声明写入；注册兼容别名，需做旧参数 smoke：list_pcg_graphs, get_pcg_info, get_pcg_graph_nodes |
| `run-pcg-graph` | `URunPCGGraphTool` | True | `Source\UEBridgeMCPPCG\Private\Tools\PCG\PCGGraphTools.cpp` | 声明写入；声明 dry_run；注册兼容别名，需做旧参数 smoke：execute_pcg |
