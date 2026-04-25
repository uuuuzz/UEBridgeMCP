# UEBridgeMCP 能力矩阵

权威口径：

- 基础编辑器工具面的源码入口是 `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp :: RegisterBuiltInTools()`。
- 运行时权威清单是 `tools/list`。
- 运行时权威数量是 `initialize.capabilities.tools.registeredCount`。
- Step 6 之后，工具数量会随着条件工具和扩展模块工具的出现而变化，所以本项目不再承诺单一固定总数。

状态说明：

- `已有`：已经存在稳定的一线工具面。
- `部分已有`：已经能做，但仍有 v1 边界或明显缺口。
- `缺失`：还没有专门工具面。
- `不做（核心）`：故意不放进核心编辑器模块，只通过可选扩展模块暴露。

截至 `2026-04-24`：

- 基础编辑器工具面：已补入 2026-04-24 能力扩展批次；准确运行时数量以 `tools/list` 为准。
- 额外运行时工具会随着条件编辑器能力和 Step 6 扩展模块一起增减。

| 能力族 | 状态 | 当前 UEBridgeMCP 覆盖 | 计划步骤 / 备注 |
|---|---|---|---|
| 治理 / provenance 基线 | 已有 | `CapabilityMatrix`、`ProvenanceTemplate`、`ToolDevelopment`、`Tools-Reference`、临时路线图与 checklist 文档 | Step 0 已完成 |
| Blueprint 作者能力 | 部分已有 | `query-blueprint-summary`、`query-blueprint-graph-summary`、`query-blueprint-node`、`query-blueprint-findings`、`analyze-blueprint-compile-results`、`edit-blueprint-graph`、`edit-blueprint-members`、`create-blueprint-function`、`create-blueprint-event`、`edit-blueprint-function-signature`、`manage-blueprint-interfaces`、`layout-blueprint-graph`、`apply-blueprint-fixups`、`create-blueprint-pattern`、`edit-blueprint-components`、`compile-assets`、`auto-fix-blueprint-compile-errors`、`blueprint-scaffold-from-spec` | Blueprint Round 1 已闭环。Phase 1C 已在 `MyProject` 中实现并完成 smoke：编译分析、结构性 fixup、interface conform、3 个精选 Actor pattern，以及可选测量式图布局模式。后续仍保留语义级图重写和非 Actor pattern catalog |
| Widget / UMG / CommonUI | 部分已有 | `inspect-widget-blueprint`、`add-widget`、`create-widget-blueprint`、`edit-widget-blueprint`、`edit-widget-layout-batch`、`edit-widget-animation`、`edit-widget-component`、`create-common-ui-widget`、`edit-common-ui`、`query-common-ui-widgets`、prompt `widget-ui-from-spec-brief` | Step 1 加 2026-04-24 CommonUI 对齐批次已完成；spec-to-UI prompt 负责把 AI 侧规划落到显式 Widget 工具调用；更深的 CommonUI style/theme authoring 留给后续 |
| Actor / Level / Spatial | 部分已有 | `query-level-summary`、`query-actor-detail`、`query-actor-selection`、`query-spatial-context`、`query-world-summary`、`edit-level-batch`、`align-actors-batch`、`drop-actors-to-surface`、`generate-level-structure`、`capture-viewport` | Step 2 已完成，继续保留 smoke checklist |
| StaticMesh / Material / Environment | 部分已有 | `query-static-mesh-summary`、`query-mesh-complexity`、`edit-static-mesh-settings`、`edit-static-mesh-slots`、`replace-static-mesh`、`query-material-summary`、`query-material-instance`、`edit-material-instance-batch`、`create-material-instance`、`edit-material-graph`、`apply-material`、`query-environment-summary`、`edit-environment-lighting` | Step 3 加 2026-04-24 StaticMesh 对齐批次已完成；更深层的 MaterialFunction、云和天气仍在后续 |
| Data / AssetManagement / Search / References | 部分已有 | `create-asset`、`create-user-defined-struct`、`create-user-defined-enum`、`add-datatable-row`、`query-asset`、`query-datatable`、`edit-datatable-batch`、`manage-assets`、`manage-asset-folders`、`import-assets`、`source-control-assets`、`get-asset-diff`、`get-class-hierarchy`、`query-engine-api-symbol`、`query-class-member-summary`、`query-plugin-capabilities`、`query-editor-subsystem-summary`、`find-references`、`search-project`、`search-assets-advanced`、`search-blueprint-symbols`、`search-level-entities`、`search-content-by-class`、`query-unused-assets` | Step 4、Search Phase 4A 和 Engine API Phase 4B 已完成；`create-asset` 覆盖运行时 BlueprintInterface、已初始化 LevelSequence 和保存型 `FoliageType_InstancedStaticMesh` 创建；Search 覆盖 ranked asset/class/symbol/level/project 查询；Engine API helper 覆盖本地反射 symbol、class member、plugin capability 和 subsystem availability。C++/config/source full-text indexing 与在线文档镜像仍留给后续 |
| Gameplay / Input / AI / Navigation / Networking | 部分已有 | `create-input-action`、`create-input-mapping-context`、`edit-input-mapping-context`、`manage-gameplay-tags`、`query-gas-asset-summary`、`query-ability-system-state`、`create-gameplay-ability`、`create-gameplay-effect`、`create-attribute-set`、`edit-gameplay-effect-modifiers`、`manage-ability-system-bindings`、`create-gameframework-blueprint-set`、`create-ai-behavior-assets`、`query-ai-behavior-assets`、`edit-blackboard-keys`、`query-navigation-state`、`query-navigation-path`、`edit-navigation-build`、`edit-statetree-bindings`、`query-replication-summary`、`edit-replication-settings`、`edit-network-component-settings`、`query-runtime-actor-state`，以及 PIE 与反射调用工具 | Step 5 加 2026-04-24 AI、Navigation、Networking 对齐批次已完成；Behavior Tree 图编辑和多人运行时 session orchestration 留给后续 |
| Physics / Collision / Constraints | 部分已有 | `query-physics-summary`、`trace-gameplay-collision`、`edit-collision-settings`、`edit-physics-simulation`、`create-physics-constraint`、`edit-physics-constraint`、`apply-physical-material` | Physics Phase 3A 以及 Gameplay Runtime Phase 3C collision trace 已在 `MyProject` 中实现并完成 smoke：actor/world 摘要、collision settings、simulation settings、PhysicalMaterial override、constraint create/edit、PIE line trace、dry-run 路径与结构化负路径。运行时长时间物理仿真断言、破坏性物理世界修改和更深层 Chaos diagnostics 留给后续 |
| Animation / Montage / AnimBlueprint | 部分已有 | `query-animation-asset-summary`、`query-skeleton-summary`、`create-animation-montage`、`create-blend-space`、`edit-blend-space-samples`、`edit-animation-notifies`、`edit-anim-graph-node`、`edit-anim-blueprint-state-machine`、`compile-assets` | Step 6 加 2026-04-24 Animation 对齐批次已完成；状态机和 AnimGraph 编辑仍保持受限结构性 v1 工具面 |
| Sequencer / ControlRig | 部分已有 | `query-level-sequence-summary` 与 `edit-sequencer-tracks` 由编辑器模块按条件注册；`edit-control-rig-graph` 由可选 `UEBridgeMCPControlRig` 模块提供 | Sequencer Round 1 已在 `MyProject` 中实现并完成 smoke：运行时创建 LevelSequence、已初始化 MovieScene 摘要、playback range、Actor binding、transform/bool tracks、通过引擎专用路径创建 Camera Cut、dry-run、save 与缺失资产结构化负路径。Control Rig 仍保持扩展模块形态 |
| 世界生产力（Spline / Landscape / Foliage / WorldPartition / PCG） | 部分已有 | `edit-spline-actors`、条件注册的 `query-landscape-summary`、条件注册的 `create-landscape`、条件注册的 `edit-landscape-region`、条件注册的 `query-foliage-summary`、条件注册的 `edit-foliage-batch`、条件注册的 `query-worldpartition-cells`、条件注册的 `edit-worldpartition-cells`、可选模块 `generate-pcg-scatter`、`query-pcg-graph-summary`、`edit-pcg-graph`、`run-pcg-graph` | Step 6 加 2026-04-24 World Partition 与 PCG 对齐批次已完成；更深的 procedural foliage 和 landscape material/layer authoring 留给后续 |
| Niagara / Audio / MetaSound | 部分已有 | 条件注册的 Niagara Phase 2A 工具：`query-niagara-system-summary`、`query-niagara-emitter-summary`、`create-niagara-system-from-template`、`edit-niagara-user-parameters`、`apply-niagara-system-to-actor`；Audio Phase 2B 工具：`query-audio-asset-summary`、`create-sound-cue`、`edit-sound-cue-routing`、`create-audio-component-setup`、`apply-audio-to-actor`；条件注册的 MetaSound Phase 2C 工具：`query-metasound-summary`、`create-metasound-source`、`edit-metasound-graph`、`set-metasound-input-defaults` | Niagara Phase 2A、Audio Phase 2B 和 MetaSound Phase 2C 均已在 `MyProject` 中实现并完成 smoke。MetaSound v1 覆盖 Source 创建、摘要查询、graph input 默认值、受限 graph I/O/layout 编辑和结构化负路径；任意 MetaSound 图生成仍留给后续 |
| Performance / Diagnostics | 部分已有 | `get-logs`、`capture-viewport`、`trigger-live-coding`、`build-and-relaunch`、`query-performance-report`、`capture-performance-snapshot`、`query-render-stats`、`query-memory-report`、`profile-visible-actors`、`edit-editor-selection`、`edit-viewport-camera`、`run-editor-command` | Step 6 加 2026-04-24 编辑器交互与性能细节批次已完成；不进入 Unreal Insights 级深度 trace |
| Workflow / Orchestration / Build | 已有 | `manage-workflow-presets`、`run-workflow-preset`、`run-editor-macro`、`run-project-maintenance-checks`、`query-workspace-health`、`generate-blueprint-pattern`、`generate-level-pattern`、`blueprint-scaffold-from-spec`、`query-gameplay-state`、`auto-fix-blueprint-compile-errors`、`generate-level-structure` 以及构建辅助工具 | Step 6 把这一层补成了资源、prompt 和 preset 体系；Macro / Utility Phase 4C 已在 `MyProject` 完成 smoke，覆盖 curated editor macros、workspace health、maintenance checks、Blueprint pattern wrapper 和 engine-only level patterns；万能脚本执行器仍然不进入本轮范围 |
| Resources / Prompts / Presets | 已有 | `resources/list`、`resources/read`、`prompts/list`、`prompts/get`、内置 workflow resources、内置 prompts、项目 preset 存储与执行 | Step 6 已完成；内容来自仓库文本资源并保持 UEBridgeMCP 自写风格 |
| External AI generation | 不做（核心） | `generate-external-content` 与 `generate-external-asset` 位于可选 `UEBridgeMCPExternalAI` 模块，通过 provider/settings 分层并使用 HTTP/JSON | Step 6 加 2026-04-24 外部资产 payload/import-plan 工具已作为扩展模块能力完成，仍然不把第三方服务依赖塞进核心编辑器模块 |

补充说明：

- Step 6 明确把项目口径从“固定总数”改成了“运行时动态清单”。
- `get-project-info` 现在会返回 Sequencer、Control Rig、Landscape、Foliage、World Partition、PCG、Niagara、MetaSound、External AI 的 `optional_capabilities`。
- 2026-04-24 兼容批次新增名称兼容别名；`Validation/Smoke/VerifyCompatibilityAliases.ps1` 会检查 alias target 和核心期望映射。
- 路线图继续按工作流能力族推进，而不是和任何第三方插件做 1:1 工具对齐。
