# UEBridgeMCP 工具速查手册

权威口径：

- 运行时工具清单，以编辑器返回的 `tools/list` 为准。
- 运行时工具数量，以 `initialize.capabilities.tools.registeredCount` 为准。
- `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp :: RegisterBuiltInTools()` 是基础编辑器工具面的源码入口。
- Step 6 之后，协议层新增了 `resources/*` 和 `prompts/*`，同时又引入了条件注册工具和扩展模块工具，所以运行时工具数量不再承诺为固定常数。

当前清单模型：

- 基础编辑器工具面：直接在 `RegisterBuiltInTools()` 中注册的 always-on 工具。
- 额外的核心条件工具会在相关引擎模块可用时出现：
  - `query-level-sequence-summary`
  - `edit-sequencer-tracks`
  - `query-landscape-summary`
  - `create-landscape`
  - `edit-landscape-region`
  - `query-foliage-summary`
  - `edit-foliage-batch`
  - `query-worldpartition-cells`
  - `query-niagara-system-summary`
  - `query-niagara-emitter-summary`
  - `create-niagara-system-from-template`
  - `edit-niagara-user-parameters`
  - `apply-niagara-system-to-actor`
  - `query-metasound-summary`
  - `create-metasound-source`
  - `edit-metasound-graph`
  - `set-metasound-input-defaults`
- 额外的扩展模块工具会在对应模块加载后出现：
  - `edit-control-rig-graph`
  - `generate-pcg-scatter`
  - `query-pcg-graph-summary`
  - `edit-pcg-graph`
  - `run-pcg-graph`
  - `generate-external-content`
  - `generate-external-asset`

所以，Step 6 之后不要再把任何单一固定数字当成最终真值。发布前建议运行 `Validation/Smoke/Invoke-ReleasePreflight.ps1`；它会把实时 `registeredCount`、`tools/list`、resources、prompts、兼容 alias 和安全探针结果记录到 `Tmp/Validation/ReleasePreflight/<timestamp>/summary.json`。

## 协议面

UEBridgeMCP 现在正式支持这些 MCP 方法：

- `initialize`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/read`
- `prompts/list`
- `prompts/get`

`initialize` 会暴露：

- `capabilities.tools`
- `capabilities.resources`
- `capabilities.prompts`
- `capabilities.tools.registeredCount`

## 内置 Resources

内置资源来自仓库里的文本文件，位于 `Resources/MCP/Resources/`，运行时只读。

| URI | 名称 | 用途 |
|---|---|---|
| `uebmcp://builtin/resources/animation-smoke-checklist` | Animation Smoke Checklist | 动画作者工具的最小 smoke checklist |
| `uebmcp://builtin/resources/sequencer-edit-recipe` | Sequencer Edit Recipe | Sequencer 最小安全编辑顺序与验证 recipe |
| `uebmcp://builtin/resources/world-production-recipe` | World Production Recipe | Spline、Foliage、Landscape、World Partition 等世界生产力工作流指引 |
| `uebmcp://builtin/resources/performance-triage-guide` | Performance Triage Guide | Editor / PIE 性能排查与证据采集指南 |
| `uebmcp://builtin/resources/external-content-safety-guide` | External Content Safety Guide | 外部内容生成的 provenance 与安全说明 |

## 内置 Prompts

内置 prompts 来自 `Resources/MCP/Prompts/` 下的 JSON 模板。

| Prompt 名称 | 用途 | 关键参数 |
|---|---|---|
| `animation-workflow-brief` | 生成简洁的动画工作流 brief | `goal`, `asset_path`, `notes` |
| `sequencer-edit-brief` | 生成 Sequencer 安全编辑 brief | `goal`, `sequence_path`, `shot_notes` |
| `performance-triage-brief` | 生成性能排查 brief | `goal`, `world_mode`, `focus_area` |

## Workflow Presets

Step 6 新增了项目级 workflow presets：

- `manage-workflow-presets`
- `run-workflow-preset`

Preset 文件固定存放在 `Config/UEBridgeMCP/WorkflowPresets/*.json`。

Preset schema 至少包含：

- `id`
- `title`
- `description`
- `resource_uris[]`
- `prompt_name`
- `tool_calls[]`
- `default_arguments{}`

`run-workflow-preset` 支持：

- `dry_run=true` 时只展开资源、prompt 和最终工具调用计划
- `dry_run=false` 时按顺序真正执行 `tool_calls[]`

## 基础工具面

下面这些工具属于基础编辑器工具面，由 `RegisterBuiltInTools()` 直接注册。

### 1. Blueprint 查询

- `query-blueprint-summary`
- `query-blueprint-graph-summary`
- `query-blueprint-node`
- `query-blueprint-findings`
- `analyze-blueprint-compile-results`

### 2. Animation 与 Performance 查询

- `query-animation-asset-summary`
- `query-skeleton-summary`
- `query-performance-report`
- `capture-performance-snapshot`
- `query-render-stats`
- `query-memory-report`
- `profile-visible-actors`
- `query-physics-summary`

### 3. Level / World 查询

- `query-level-summary`
- `query-actor-detail`
- `query-actor-selection`
- `query-spatial-context`
- `query-world-summary`

### 4. Material / StaticMesh / Audio / Environment 查询

- `query-material-summary`
- `query-material-instance`
- `query-static-mesh-summary`
- `query-mesh-complexity`
- `query-audio-asset-summary`
- `query-environment-summary`

### 5. Project / Asset / Utility 查询

- `get-project-info`
- `query-asset`
- `query-datatable`
- `get-asset-diff`
- `get-class-hierarchy`
- `query-engine-api-symbol`
- `query-class-member-summary`
- `query-plugin-capabilities`
- `query-editor-subsystem-summary`
- `query-workspace-health`
- `find-references`
- `search-project`
- `search-assets-advanced`
- `search-blueprint-symbols`
- `search-level-entities`
- `search-content-by-class`
- `query-unused-assets`
- `inspect-widget-blueprint`
- `get-logs`

### 6. Widget / UMG 作者工具

- `create-widget-blueprint`
- `edit-widget-blueprint`
- `edit-widget-layout-batch`
- `edit-widget-animation`
- `edit-widget-component`
- `create-common-ui-widget`
- `edit-common-ui`
- `query-common-ui-widgets`

兼容说明：

- `add-widget` 仍然保留，但新的写路径优先使用上面的批处理 Widget 工具。

### 7. Create 与 Data Authoring

- `create-asset`
- `create-user-defined-struct`
- `create-user-defined-enum`
- `add-component`
- `add-widget`
- `add-datatable-row`
- `spawn-actor`

Create 说明：

- `create-asset` 现在显式支持 `BlueprintInterface`、`LevelSequence` 和 `FoliageType_InstancedStaticMesh`，连同 Blueprint、WidgetBlueprint、AnimBlueprint、Material、DataTable、DataAsset 以及基于类路径的通用创建一起提供。
- 创建 Blueprint Interface 时，使用 `asset_class="BlueprintInterface"`，并传正常资产路径，例如 `/Game/UEBridgeMCPValidation/BlueprintRound1/BPI_BlueprintRound1_20260423_123000`。
- 对 `BlueprintInterface` 来说，`parent_class` 会被忽略；接口资产走引擎内置的 Blueprint Interface factory 创建。
- 创建 AnimBlueprint 时，使用 `asset_class="AnimBlueprint"`，并把 `parent_class` 设为 Skeleton 资产路径。AnimBlueprint 创建现在走引擎 `UAnimBlueprintFactory`，因此结果是真正带 target skeleton 和默认 AnimGraph 的 `AnimBlueprint` 资产。
- 创建 LevelSequence 时，使用 `asset_class="LevelSequence"`。工具会调用 `ULevelSequence::Initialize()`，因此新资产在后续 Sequencer 编辑前已经拥有真实 MovieScene。
- 创建 Instanced Static Mesh Foliage Type 时，使用 `asset_class="FoliageType_InstancedStaticMesh"` 并传 `static_mesh_path`。在 World Partition 地图中，推荐先创建保存型 foliage type 资产，再用 `edit-foliage-batch` 的 `foliage_type_path` 添加实例。

### 8. StateTree

- `query-statetree`
- `add-statetree-state`
- `remove-statetree-state`
- `add-statetree-transition`
- `add-statetree-task`
- `edit-statetree-bindings`

### 9. Gameplay / Input / AI / Navigation / Networking

- `create-input-action`
- `create-input-mapping-context`
- `edit-input-mapping-context`
- `manage-gameplay-tags`
- `query-gas-asset-summary`
- `create-gameplay-ability`
- `create-gameplay-effect`
- `create-attribute-set`
- `edit-gameplay-effect-modifiers`
- `manage-ability-system-bindings`
- `query-ability-system-state`
- `create-gameframework-blueprint-set`
- `create-ai-behavior-assets`
- `query-navigation-state`
- `query-navigation-path`
- `edit-navigation-build`
- `query-ai-behavior-assets`
- `edit-blackboard-keys`
- `query-replication-summary`
- `edit-replication-settings`
- `query-network-component-settings`
- `edit-network-component-settings`
- `trace-gameplay-collision`
- `edit-collision-settings`
- `edit-physics-simulation`
- `create-physics-constraint`
- `edit-physics-constraint`

### 10. Scripting / Build / PIE / Reflection

- `run-python-script`
- `trigger-live-coding`
- `build-and-relaunch`
- `pie-session`
- `pie-input`
- `wait-for-world-condition`
- `assert-world-state`
- `query-runtime-actor-state`
- `query-ability-system-state`
- `trace-gameplay-collision`
- `call-function`
- `edit-editor-selection`
- `edit-viewport-camera`
- `run-editor-command`

### 11. Batch Edit 与 Asset Lifecycle

- `edit-blueprint-graph`
- `edit-blueprint-members`
- `create-blueprint-function`
- `create-blueprint-event`
- `edit-blueprint-function-signature`
- `manage-blueprint-interfaces`
- `layout-blueprint-graph`
- `apply-blueprint-fixups`
- `edit-blueprint-components`
- `add-graph-node`
- `connect-graph-pins`
- `disconnect-graph-pin`
- `remove-graph-node`
- `set-property`
- `edit-datatable-batch`
- `edit-level-actor`
- `edit-level-batch`
- `align-actors-batch`
- `drop-actors-to-surface`
- `edit-static-mesh-settings`
- `edit-static-mesh-slots`
- `replace-static-mesh`
- `edit-material-instance-batch`
- `create-material-instance`
- `edit-material-graph`
- `create-sound-cue`
- `edit-sound-cue-routing`
- `create-audio-component-setup`
- `apply-audio-to-actor`
- `edit-gameplay-effect-modifiers`
- `manage-ability-system-bindings`
- `edit-environment-lighting`
- `create-animation-montage`
- `create-blend-space`
- `edit-blend-space-samples`
- `edit-animation-notifies`
- `edit-anim-graph-node`
- `edit-anim-blueprint-state-machine`
- `compile-assets`
- `manage-assets`
- `manage-asset-folders`
- `import-assets`
- `source-control-assets`
- `capture-viewport`
- `apply-material`
- `apply-physical-material`
- `edit-spline-actors`

### 12. Workflow 与高层编排

- `manage-workflow-presets`
- `run-workflow-preset`
- `run-editor-macro`
- `run-project-maintenance-checks`
- `blueprint-scaffold-from-spec`
- `create-blueprint-pattern`
- `generate-blueprint-pattern`
- `query-gameplay-state`
- `auto-fix-blueprint-compile-errors`
- `generate-level-structure`
- `generate-level-pattern`

Blueprint Phase 1C 说明：

- `analyze-blueprint-compile-results` 会在内存中编译 Blueprint，并把编译 diagnostics 与静态 Blueprint findings 归一化为 `issues[]` 和 `suggested_fixups`。
- `apply-blueprint-fixups` 只执行安全的结构性修复：`refresh_all_nodes`、`reconstruct_invalid_nodes`、`remove_orphan_pins`、`recompile_dependencies`、`conform_implemented_interfaces`。
- `create-blueprint-pattern` 是高层精选 Actor pattern 入口，v1 支持 `logic_actor_skeleton`、`toggle_state_actor`、`interaction_stub_actor`。
- `blueprint-scaffold-from-spec` 仍然是低层 spec scaffold 工具，不作为 Phase 1C curated pattern 的底层实现路径。

Niagara Phase 2A 说明：

- Niagara 工具在引擎 `Niagara` / `NiagaraEditor` 模块可用时才会从核心编辑器模块条件注册。
- `query-niagara-system-summary` 和 `query-niagara-emitter-summary` 覆盖 system / emitter 摘要、user parameter、emitter handle 和 renderer 摘要。
- `create-niagara-system-from-template` 支持通过 Niagara editor factory 创建空系统，或复制已有 template system。
- `edit-niagara-user-parameters` 支持 v1 标量、向量和颜色 user parameter 的批量 add/remove/rename/default-value 编辑。
- `apply-niagara-system-to-actor` 可以在编辑器 World Actor 上创建或更新 NiagaraComponent，并写入组件级 user parameter override；editor-world 的 `activate_now` 会返回 deferred warning，而不是直接在 editor GameThread 上启动 simulation。

Audio Phase 2B 说明：

- Audio 工具作为核心编辑器 always-on 工具注册，并通过引擎 `AudioEditor` 支持创建 SoundCue。
- `query-audio-asset-summary` 覆盖 `SoundWave` 和 `SoundCue` 摘要，包括 SoundCue 节点路由结构。
- `create-sound-cue` 可以从可选的 `initial_sound_wave_path` 创建 SoundCue，并设置基础 volume/pitch。
- `edit-sound-cue-routing` 使用批量 `operations[]` 编辑 wave-player、random、mixer、attenuation wrap、multiplier，并覆盖 dry-run、rollback 和 save 路径。
- `create-audio-component-setup` 与 `apply-audio-to-actor` 可以在 Actor 上创建或更新 `AudioComponent`；editor-world 的 `play_now` 会返回 deferred warning，而不是直接在 editor GameThread 上播放。

MetaSound Phase 2C 说明：

- MetaSound 工具在 `MetasoundEngine`、`MetasoundFrontend`、`MetasoundEditor` 可用时才会从核心编辑器模块条件注册。
- `query-metasound-summary` 覆盖 MetaSound Source 摘要、graph input/output、interface，以及可选的默认图节点和连线。
- `create-metasound-source` 通过引擎 Source Builder 创建新的 MetaSound Source，并支持 v1 graph input 和 output format 设置。
- `set-metasound-input-defaults` 支持 bool/int32/float/string graph input 默认值的批量编辑，并覆盖 dry-run、rollback 和 save 路径。
- `edit-metasound-graph` 有意限制在 v1 结构性编辑：graph I/O、按 class name 插入节点、显式连接、节点 input 默认值和 layout。任意 MetaSound 图生成留给后续阶段。

Physics Phase 3A 说明：

- Physics 工具作为核心编辑器 always-on 工具注册，并使用引擎 `PhysicsCore` 与 editor-world Actor / Component API。
- `query-physics-summary` 返回 world 或 actor 级物理摘要，包括 PrimitiveComponent 的 collision / simulation 状态，以及 PhysicsConstraintComponent 明细。
- `edit-collision-settings` 支持 collision profile、enabled mode、object channel、全通道 response 与单通道 response 修改，并覆盖 dry-run、rollback 和 save 路径。
- `edit-physics-simulation` 支持 simulate physics、gravity、mass override、mass scale、linear / angular damping、mobility 调整、wake 和 sleep。
- `create-physics-constraint` 与 `edit-physics-constraint` 覆盖基础双组件约束、disable-collision、projection、linear limits、angular limits 与 break threshold。
- `apply-physical-material` 可以给 PrimitiveComponent 设置 PhysicalMaterial override；运行时物理播放和仿真断言留给后续阶段。

GAS Phase 3B 说明：
- GAS 工具作为核心编辑器 always-on 工具注册，并使用引擎 `GameplayAbilities`、`GameplayAbilitiesEditor` 与 `GameplayTasks` 支持。
- `create-attribute-set` 创建 AttributeSet Blueprint，并写入 `FGameplayAttributeData` 成员变量。
- `create-gameplay-effect` 创建 GameplayEffect Blueprint，支持 duration、period、granted target tags 与简单常量 modifier。
- `edit-gameplay-effect-modifiers` 支持 dry-run 与 rollback-safe 的 duration、period、granted tags、clear/add/remove modifier、final compile/save。
- `create-gameplay-ability` 创建 GameplayAbility Blueprint，支持 ability tags、activation tag containers、常见 policy、cost effect 与 cooldown effect 默认值。
- `query-gas-asset-summary` 摘要 GameplayAbility、GameplayEffect、AttributeSet 与 Actor Blueprint GAS bindings。
- `manage-ability-system-bindings` 给 Actor Blueprint 添加 `AbilitySystemComponent`，并用带 UEBridgeMCP metadata 的 `TSubclassOf` 变量保存 ability/effect/attribute-set 绑定。
- v1 只覆盖资产与 Actor Blueprint 配置；runtime granting、prediction validation、复杂 execution calculation 和 ability task graph authoring 留给后续阶段。

Gameplay Runtime Phase 3C 说明：

- `query-runtime-actor-state` 返回 editor / PIE Actor 的只读运行时状态，包括 transform、bounds、velocity、actor tags、components、collision 摘要和可选 GAS 状态。
- `query-ability-system-state` 返回 editor / PIE Actor 上实时 `AbilitySystemComponent` 状态，包括 owned gameplay tags、spawned AttributeSets 和 activatable abilities。
- `trace-gameplay-collision` 在 editor / PIE world 中执行只读 line、sphere、capsule 或 box trace，并返回结构化 hit actor / component 数据。
- Phase 3C 已覆盖 PIE 实时查询和 collision trace；runtime grant / activate ability、prediction validation、长时间 physics simulation 断言仍留给后续阶段。

Search Phase 4A 说明：

- `search-assets-advanced` 提供 asset name / path / class 字段上的 ranked search，支持 exact、wildcard、contains、camel-case 和 fuzzy subsequence 匹配。
- `search-content-by-class` 是 class-first asset search 入口，`class` 必填，同时支持 query 与 path filters。
- `search-blueprint-symbols` 搜索 Blueprint variables、function graphs、macro graphs、event graphs，并可选扫描 graph nodes；通过 `max_blueprints` 限流。
- `search-level-entities` 搜索 editor / PIE actors 的 label、name、class、folder、tag，并返回 actor handle 与可选 transform / bounds。
- `search-project` 聚合 assets、Blueprint symbols、level entities，返回 per-section 结果和 flattened ranked list。

Engine API Phase 4B 说明：

- `query-engine-api-symbol` 搜索本地已加载反射数据中的 classes、functions、properties、structs、enums、subsystems 和 plugins。
- `query-class-member-summary` 解析 class name 或 path，并返回 function / property 摘要、flags、参数和可选 metadata。
- `query-plugin-capabilities` 返回本地 plugin enablement、mounted/content/Verse support、descriptor metadata 与 module loading state。
- `query-editor-subsystem-summary` 列出 Editor / Engine / World / GameInstance subsystem class，并在上下文存在时报告当前 instance availability。
- Phase 4B 只做本地辅助解释，不代理外部文档站点，也不建立在线文档镜像。

## 核心条件工具

这些工具在核心编辑器模块里按“相关引擎模块是否可用”决定是否注册。

| 工具 | 常见依赖 | 说明 |
|---|---|---|
| `query-level-sequence-summary` | `LevelSequence`, `MovieSceneTracks` | 摘要 playback range、binding、track、section 和 Camera Cut |
| `edit-sequencer-tracks` | `LevelSequence`, `MovieSceneTracks` | v1 聚焦 binding、Camera Cut、基础 track、section、key 与 playback range |
| `query-landscape-summary` | `Landscape` | 汇总 Landscape actor、component、layer、bounds，并可选采样高度 |
| `create-landscape` | `Landscape` | 在 editor world 创建小型平坦 Landscape，用于 blockout 和验证 fixture |
| `edit-landscape-region` | `Landscape` | v1 支持矩形区域的地形和图层修改 |
| `query-foliage-summary` | `Foliage` | 按 foliage type 和 static mesh 汇总 editor 或 PIE world 中的 foliage，可选返回实例采样 |
| `edit-foliage-batch` | `Foliage` | v1 支持实例增删、bounds 内删除、transform 修改和 mesh 替换 |
| `query-worldpartition-cells` | 引擎 World Partition 支持 | 在非 World Partition 地图上会返回结构化 no-op 或 unsupported 结果 |
| `edit-worldpartition-cells` | 引擎 World Partition 支持 | 在引擎支持的情况下加载或卸载 World Partition runtime cell |
| `query-niagara-system-summary` | `Niagara`, `NiagaraEditor` | Niagara system、emitter、renderer、readiness 和 user parameter 摘要 |
| `query-niagara-emitter-summary` | `Niagara`, `NiagaraEditor` | Niagara emitter asset 或 system emitter handle 摘要 |
| `create-niagara-system-from-template` | `Niagara`, `NiagaraEditor` | 从 template 或 factory-backed empty/default system 创建 Niagara system |
| `edit-niagara-user-parameters` | `Niagara`, `NiagaraEditor` | v1 user parameter 批量编辑，支持 dry-run、compile、save |
| `apply-niagara-system-to-actor` | `Niagara`, `NiagaraEditor` | 给 editor-world Actor 创建或更新 NiagaraComponent |
| `query-metasound-summary` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | MetaSound Source 摘要与可选默认图结构 |
| `create-metasound-source` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | 通过官方 Source Builder 创建 MetaSound Source |
| `edit-metasound-graph` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | 受限 v1 MetaSound graph I/O、连接、默认值和 layout 编辑 |
| `set-metasound-input-defaults` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | 批量编辑受支持 graph input 默认值，支持 dry-run 和 rollback |

## 扩展模块工具

这些工具故意不塞进核心编辑器模块，而是放在独立扩展模块里。

| 工具 | 模块 | 说明 |
|---|---|---|
| `edit-control-rig-graph` | `UEBridgeMCPControlRig` | 可选的 Control Rig 图编辑工具 |
| `generate-pcg-scatter` | `UEBridgeMCPPCG` | 可选的 PCG scatter 生成工具，支持绑定已有图或创建 scaffold graph |
| `query-pcg-graph-summary` | `UEBridgeMCPPCG` | 汇总 PCG graph 的 node、pin、edge 和 settings |
| `edit-pcg-graph` | `UEBridgeMCPPCG` | 在受限 v1 范围内新增、删除、修改和连接 PCG graph node |
| `run-pcg-graph` | `UEBridgeMCPPCG` | 对选中 Actor 或指定 Actor 路径触发 PCG component generation |
| `generate-external-content` | `UEBridgeMCPExternalAI` | 可选的外部 HTTP/JSON 内容生成工具 |
| `generate-external-asset` | `UEBridgeMCPExternalAI` | 生成外部资产 payload 与显式 import plan，v1 不直接写入导入资产 |

`generate-external-content` 的边界固定为：

- 不进入核心编辑器工具面
- 通过 provider/settings 抽象层工作
- v1 只支持文本和 JSON 输出
- 不下载、不导入二进制媒体

Macro / Utility Phase 4C 说明：

- `query-workspace-health` 返回只读项目状态快照，包括 project paths、UEBridgeMCP plugin/server、editor/PIE world、optional capabilities、validation paths 和 dirty packages。
- `run-project-maintenance-checks` 组合常见维护检查：workspace health、保守 unused asset candidates、可选 Blueprint compile check。
- `generate-blueprint-pattern` 是面向 workflow 的 `create-blueprint-pattern` wrapper，保持相同 Actor catalog，并补上 dry-run plan。
- `generate-level-pattern` 在 editor world 中用 engine-only StaticMeshActor 创建 `test_anchor_pair`、`interaction_test_lane`、`lighting_blockout_minimal` 等精选 level scaffold。
- `run-editor-macro` 只提供精选 macro catalog：`collect_workspace_health`、`run_maintenance_checks`、`compile_blueprint_assets`、`cleanup_generated_actors`。
- Phase 4C 明确不新增万能脚本执行器；需要脚本时继续显式使用现有 `run-python-script`。

Animation Round 2 收口说明：

- `edit-anim-blueprint-state-machine` 现在支持 `create_state_machine` 和 `ensure_state_machine`，并支持可选 `connect_to_output`，因此正向状态机 smoke 不再依赖预制 AnimBP state machine fixture。
- 自包含宿主 smoke 通过 `create-asset(asset_class="AnimBlueprint")` 创建 AnimBlueprint，再创建 `Locomotion` state machine、添加两个 state、设置 entry state、添加 transition、绑定 sequence、查询摘要并编译资产。
- 证据：`Tmp/Validation/AnimationRound2/20260423_223834/summary.json`。

Sequencer Round 1 收口说明：

- `create-asset(asset_class="LevelSequence")` 现在会创建已初始化的真实 `ULevelSequence`，并带有 MovieScene，因此 Sequencer smoke 不再依赖预制 sequence 资产。
- `query-level-sequence-summary` 返回 playback range、frame rate、binding、possessable、spawnable、master/binding track、section 与 Camera Cut 摘要。
- `edit-sequencer-tracks` 现在走引擎专用 Camera Cut 路径，不再把 Camera Cut 误创建成普通 master track，因此 `GetCameraCutTrack()` 与摘要查询保持一致。
- 证据：`Tmp/Validation/SequencerRound1/20260423_225804/summary.json`。

## Optional Capability Reporting

`get-project-info` 现在会返回 `optional_capabilities`，至少包含：

- `sequencer_available`
- `control_rig_available`
- `landscape_available`
- `foliage_available`
- `world_partition_available`
- `pcg_available`
- `niagara_available`
- `metasound_available`
- `external_ai_available`

客户端应该联合使用 `optional_capabilities` 和 `tools/list`，不要再假设 Step 6 的工具总数固定不变。

## UnrealMCPServer 兼容别名

UEBridgeMCP 现在会为常见 UnrealMCPServer 风格的 `snake_case` 工具名注册一层“名称兼容”别名。

- alias 调用会通过 `FMcpToolRegistry::ResolveToolName()` 解析到 canonical 工具。
- `tools/list` 会包含 alias definition，description 中会标明 canonical target。
- UEBridgeMCP 的 canonical schema 仍是权威；alias 不会自动改写旧参数 payload。
- 静态 smoke 脚本位于 `Validation/Smoke/VerifyCompatibilityAliases.ps1`。
- 发布前总预检位于 `Validation/Smoke/Invoke-ReleasePreflight.ps1`，会补充运行时 alias 调用和安全探针。

## 如何检查运行时清单

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"check","version":"1.0"}}}'
```

然后再调用：

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
```

Step 6 之后，不同机器看到的数量不同是正常的，只要它和 `registeredCount`、可选模块状态一致即可。

## 另请参阅

- [Tool Development Guide](./ToolDevelopment.zh-CN.md)
- [Architecture](./Architecture.zh-CN.md)
- [Troubleshooting](./Troubleshooting.zh-CN.md)
- [Capability Matrix](./CapabilityMatrix.zh-CN.md)
