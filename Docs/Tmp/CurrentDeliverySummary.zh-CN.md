# UEBridgeMCP 本次新增内容汇总

## 0. 这份文档记录什么

这份汇总记录本轮从 `Step 0` 到 `Step 6` 的主线交付，以及随后完成的 `Step 6` 验收闭环。它的用途不是替代正式参考文档，而是给当前阶段一个集中、可读、可交付的快照。

## 1. 当前总体结论

- `UEBridgeMCP` 已经完成 Step 0 到 Step 6 主路线的主体交付。
- 项目已经从“能力规划”进入“增量追平和长尾扩展”阶段。
- 当前运行时口径下，`UEBridgeMCP` 在最终验收运行里是 `93` 个工具。
- 主线已经收口，但长尾工具覆盖仍有扩展空间。

## 2. Step 0 到 Step 6 已交付内容

### Step 0：文档与治理基线

已完成：

- 统一统计口径到“源码注册点 + `tools/list`”
- 新增 `CapabilityMatrix`
- 新增 `ProvenanceTemplate`
- 在工具开发文档中固化对标闭源插件时的 provenance 规则

### Step 1：Widget / UMG / CommonUI

已新增或补强：

- `inspect-widget-blueprint`
- `create-widget-blueprint`
- `edit-widget-blueprint`
- `edit-widget-layout-batch`
- `edit-widget-animation`
- `edit-widget-component`

结果：

- Widget 蓝图的创建、结构查询、布局修改、动画修改和组件编辑已经形成基础闭环。

### Step 2：Actor / Level / Spatial

已新增或补强：

- `query-actor-detail`
- `query-actor-selection`
- `query-spatial-context`
- `edit-level-batch`
- `align-actors-batch`
- `drop-actors-to-surface`

结果：

- Actor 查询、选择集、空间分析、批量关卡编辑、对齐与落地流程已经形成闭环。

### Step 3：StaticMesh / Material / Environment

已新增或补强：

- `query-static-mesh-summary`
- `edit-static-mesh-settings`
- `replace-static-mesh`
- `create-material-instance`
- `edit-material-graph`
- `query-environment-summary`
- `edit-environment-lighting`

结果：

- StaticMesh、Material、Environment 三条内容制作链已经接入到 v1 可用状态。

### Step 4：Data / AssetManagement / Search / References

已新增或补强：

- `query-datatable`
- `edit-datatable-batch`
- `create-user-defined-struct`
- `create-user-defined-enum`
- `manage-asset-folders`
- `query-unused-assets`
- 扩展 `find-references`

结果：

- DataTable、Struct、Enum、资产文件夹管理、引用分析和 unused candidate 扫描已经成体系。

### Step 5：Gameplay / Input / AI / Navigation / Networking

已新增或补强：

- `create-input-action`
- `create-input-mapping-context`
- `edit-input-mapping-context`
- `manage-gameplay-tags`
- `create-gameframework-blueprint-set`
- `create-ai-behavior-assets`
- `query-navigation-state`
- `edit-statetree-bindings`
- `query-replication-summary`
- `edit-replication-settings`

结果：

- Input、GameplayTags、AI 资产、导航、复制配置这几条玩法底座链路已经接上。

### Step 6：Animation / Sequencer / Performance / World Production / Resources / External AI

已新增或补强：

- `query-animation-asset-summary`
- `create-animation-montage`
- `edit-anim-blueprint-state-machine`
- `query-performance-report`
- `capture-performance-snapshot`
- `edit-spline-actors`
- 条件工具：
  - `edit-sequencer-tracks`
  - `edit-control-rig-graph`
  - `edit-landscape-region`
  - `edit-foliage-batch`
  - `query-worldpartition-cells`
  - `generate-pcg-scatter`
- 扩展工具：
  - `generate-external-content`
- 工作流层：
  - `manage-workflow-presets`
  - `run-workflow-preset`
- MCP 协议面：
  - `resources/list`
  - `resources/read`
  - `prompts/list`
  - `prompts/get`

结果：

- 项目已经不只是一组编辑器工具，而是具备了 resources、prompts、presets、performance snapshot 和 external AI 扩展层的完整 Step 6 结构。

## 3. Step 6 验收闭环也已经完成

本次不只是把 Step 6 的代码接上，还完成了 `MyProject` 宿主上的本地验收。

关键结论：

- 宿主工程：`G:\UEProjects\MyProject`
- 最终 accepted run：
  - `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727`
- 关键结果：
  - `UEBridgeMCP registeredCount = 93`
  - `UEBridgeMCP tools/list = 93`
- 最终结果没有遗留 blocker

### 已完成的验证范围

- `initialize`
- `tools/list`
- `resources/list/read`
- `prompts/list/get`
- workflow presets 的 `upsert -> list -> dry_run -> live -> delete`
- animation summary
- animation montage creation
- performance report
- performance snapshot
- spline editing
- World Partition 非 WP 地图负路径
- External AI 未配置与错误配置路径

### 已关闭的历史验证缺口

- `edit-anim-blueprint-state-machine` 的正向 smoke 已在 Animation Round 2 补齐
- 现在通过公开 MCP 工具创建真实 AnimBlueprint，再创建 state machine、states、transition、sequence assignment，并完成 summary query 与 compile/save
- 证据：`Tmp/Validation/AnimationRound2/20260423_223834/summary.json`
- Sequencer 的自包含 create/query/edit smoke 已在 Sequencer Round 1 补齐
- 现在通过 `create-asset(asset_class="LevelSequence")` 创建已初始化 MovieScene 的 Level Sequence，再用 `edit-sequencer-tracks` 写入 playback range、Actor binding、transform/bool track/key 和专用 Camera Cut，并用 `query-level-sequence-summary` 验证
- 证据：`Tmp/Validation/SequencerRound1/20260423_225804/summary.json`
- Foliage 的自包含 World Partition-safe smoke 已在 Foliage Round 1 补齐
- 现在通过 `create-asset(asset_class="FoliageType_InstancedStaticMesh")` 创建保存型 foliage type，再用 `query-foliage-summary` 查询、用 `edit-foliage-batch` 验证 dry-run mesh add、按 `foliage_type_path` add/remove，并覆盖 missing mesh 结构化负路径
- 证据：`Tmp/Validation/FoliageRound1/20260423_233538/summary.json`
- Landscape 的自包含 create/query/edit smoke 已在 Landscape Round 1 补齐
- 现在通过 `create-landscape` 创建小型平坦 Landscape，再用 `query-landscape-summary` 采样高度、用 `edit-landscape-region` 修改局部高度，并验证 dry-run 不改高度、真实写入使区域内高度 `0 -> 128`、区域外保持 `0`
- 证据：`Tmp/Validation/LandscapeRound1/20260423_235345/summary.json`

## 4. 本次验收里顺手修掉的 blocker

### 4.1 工具注册表关闭期崩溃

已修复：

- `McpToolRegistry` 改为使用 `TWeakObjectPtr` 持有懒加载工具实例
- 可选模块反注册和编辑器关闭路径不再因为失效对象句柄崩溃

### 4.2 External AI 超时回调踩空

已修复：

- `OpenAICompatibleExternalAIProvider` 不再在超时后让晚到 HTTP 回调访问失效栈变量
- `bad-config` 负路径现在稳定返回结构化 timeout，而不是打崩编辑器

### 4.3 MyProject 宿主下的兼容修正

已修复：

- `UEBridgeMCPControlRig` 针对当前宿主引擎环境做了兼容性修正
- `MyProjectEditor` Win64 Development 构建已恢复通过

## 5. 本次新增的验证与留档产物

### 验收文档

- `Docs/Validation/Step6/ComparisonMatrix.md`
- `Docs/Validation/Step6/ExecutionReport.md`
- `Docs/Validation/Step6/Issues.md`

### 验证脚本与样例

- `Validation/Step6/Invoke-Step6Validation.ps1`
- `Validation/Step6/Samples/Initialize.request.json`
- `Validation/Step6/Samples/PromptGet.performance-triage-brief.request.json`
- `Validation/Step6/Samples/WorkflowPreset.step6-validation.json`
- `Validation/Step6/Samples/GenerateExternalContent.bad-config.request.json`
- `Validation/Step6/Samples/QueryAnimationAssetSummary.validation-animbp.request.json`

### 运行证据

- `Tmp/Validation/Step6/20260423_114727/summary.json`
- `Tmp/Validation/Step6/20260423_114727/scenario-results.json`

## 6. 现在项目处在什么阶段

现在的 `UEBridgeMCP` 已经不再处于“先把主路线补出来”的阶段，而是进入“继续追平高价值长尾能力”的阶段。

换句话说：

- 主线六步：基本完成
- Step 6 验收闭环：已完成
- 长尾工具覆盖：还没有完成

接下来最值的工作，不是回头重做主路线，而是继续补高价值长尾：

- 第一优先：Blueprint 长尾作者工具
- 第二优先：Niagara / Audio / MetaSound
- 第三优先：Physics + GAS + 更深的 Gameplay Runtime
- 第四优先：Search / EngineAPI / Macro / Developer Utility

## 7. 一句话结论

这次新增内容已经把 `UEBridgeMCP` 从“六步路线规划”推进到了“主线交付 + Step 6 验收闭环完成”的状态。项目现在可以视为一个已完成主线、已完成验收、但仍有明显长尾扩展空间的版本。
