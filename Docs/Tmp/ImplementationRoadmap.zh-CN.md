# UEBridgeMCP 实施任务清单

这份临时路线图把能力矩阵转成执行顺序。项目始终按“能力族”推进，不按外部插件的工具名逐个复刻。

## 0. 当前状态

- `Step 0` 已完成：文档、治理基线、统计口径、provenance 模板都已收口。
- `Step 1` 已完成：Widget / UMG 基础作者工具已落地。
- `Step 2` 已完成：Actor / Level / Spatial 工具已落地。
- `Step 3` 已完成：StaticMesh / Material / Environment 工具已落地。
- `Step 4` 已完成：Data / AssetManagement / Search / References 工具已落地。
- `Step 5` 已完成：Gameplay / Input / AI / Navigation / Networking 工具已落地。
- `Step 6` 已完成代码落地：Animation / Sequencer / Performance / World Production / Resources / External AI 已接入。

Step 6 之后的权威统计规则：

- 工具清单以 `tools/list` 为准。
- 工具数量以 `initialize.capabilities.tools.registeredCount` 为准。
- 不再承诺单一固定工具总数。

## 1. 总体原则

- 先做 `Step 0` 基线，再推进六个技术阶段。
- 顺序固定为：
  - `Widget / UMG`
  - `Actor / Level / Spatial`
  - `StaticMesh / Material / Environment`
  - `Data / AssetManagement / Search / References`
  - `Gameplay / Input / AI / Navigation / Networking`
  - `Animation / Sequencer / Performance / World Production / AI Resources`
- 只对齐能力面，不复刻第三方或闭源插件的工具名、schema 原文、错误文案或模块切分。
- `External AI / Resources / Prompts / Presets` 属于主路线的一部分，但实现上保持独立扩展层或独立注册层，不把第三方服务逻辑直接揉进核心工具层。

## 2. 已完成步骤

### Step 0：文档与治理基线

已完成内容：

- 统一统计口径为“源码注册点 + `tools/list`”
- 新增 `CapabilityMatrix`
- 新增 `ProvenanceTemplate`
- 在工具开发文档中补充对标闭源产品时的 provenance 规则

### Step 1：Widget / UMG / CommonUI

已落地工具：

- `inspect-widget-blueprint`
- `create-widget-blueprint`
- `edit-widget-blueprint`
- `edit-widget-layout-batch`
- `edit-widget-animation`
- `edit-widget-component`
- `add-widget` 作为兼容入口继续保留

当前边界：

- CommonUI 只做能力探测，不复刻对方 API 语义或资源命名。

### Step 2：Actor / Level / Spatial

已落地工具：

- `query-actor-detail`
- `query-actor-selection`
- `query-spatial-context`
- `edit-level-batch`
- `align-actors-batch`
- `drop-actors-to-surface`

当前边界：

- 继续维持 batch-first 和事务式编辑，不回退到碎片化单动作工具。

### Step 3：StaticMesh / Material / Environment

已落地工具：

- `query-static-mesh-summary`
- `edit-static-mesh-settings`
- `replace-static-mesh`
- `create-material-instance`
- `edit-material-graph`
- `query-environment-summary`
- `edit-environment-lighting`

当前边界：

- 先聚焦 StaticMesh、Material、Environment 的核心工作流，不把 MaterialFunction 和云天气系统拉进这一阶段。

### Step 4：Data / AssetManagement / Search / References

已落地工具：

- `query-datatable`
- `edit-datatable-batch`
- `create-user-defined-struct`
- `create-user-defined-enum`
- `manage-asset-folders`
- `query-unused-assets`
- 扩展后的 `find-references`

当前边界：

- `query-unused-assets` 维持保守判定策略，宁可少报，不直接输出“可安全删除”结论。

### Step 5：Gameplay / Input / AI / Navigation / Networking

已落地工具：

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

当前边界：

- PIE 验证仍然以 `query-gameplay-state`、`call-function`、`pie-input` 为主，不把低层输入注入当作唯一验收标准。

## 3. Step 6 收口说明

### 3.1 协议与资源面

已接入：

- `resources/list`
- `resources/read`
- `prompts/list`
- `prompts/get`

同时：

- `initialize` 已声明 `resources` 和 `prompts` 支持开启
- `registeredCount` 已改为真实运行时数量

### 3.2 Built-in Resources / Prompts / Presets

已落地：

- repo-tracked 内置 resources
- repo-tracked 内置 prompts
- `manage-workflow-presets`
- `run-workflow-preset`

Preset 存储位置：

- `Config/UEBridgeMCP/WorkflowPresets/*.json`

### 3.3 Always-on Step 6 工具

已落地：

- `query-animation-asset-summary`
- `create-animation-montage`
- `edit-anim-blueprint-state-machine`
- `query-performance-report`
- `capture-performance-snapshot`
- `edit-spline-actors`

### 3.4 条件编辑器工具

已落地：

- `edit-sequencer-tracks`
- `edit-landscape-region`
- `edit-foliage-batch`
- `query-worldpartition-cells`

说明：

- 这些工具按模块可用性决定是否注册，或者在运行时返回结构化 unsupported / no-op 结果。

### 3.5 独立扩展模块

已落地：

- `UEBridgeMCPControlRig` -> `edit-control-rig-graph`
- `UEBridgeMCPPCG` -> `generate-pcg-scatter`
- `UEBridgeMCPExternalAI` -> `generate-external-content`

边界保持如下：

- `ControlRig` 与 `PCG` 不强绑进核心编辑器模块
- `generate-external-content` 通过 provider/settings 分层工作
- v1 只支持文本 / JSON，不负责二进制媒体下载或导入

### 3.6 可见性与能力探测

已落地：

- `get-project-info.optional_capabilities`

至少返回：

- `sequencer_available`
- `control_rig_available`
- `landscape_available`
- `foliage_available`
- `world_partition_available`
- `pcg_available`
- `external_ai_available`

## 4. 当前剩余工作

这份路线图对应的代码工作已经完成，当前剩余的是验证和打磨：

- 在运行中的编辑器里补齐 Step 6 人工 smoke
- 继续验证 `resources/*` 与 `prompts/*` 的客户端兼容性
- 验证 workflow preset 的 dry-run 与真实执行链路
- 分别验证 External AI 的未配置、配置错误、配置成功三条路径
- 继续根据真实项目使用场景决定是否加深 Step 6 工具能力，而不是盲目扩面

## 5. 公共接口规则

- 查询类统一使用 `query-*`、`inspect-*`
- 编辑类优先使用 `edit-*`、`edit-*-batch`
- 创建类统一使用 `create-*`
- 治理类统一使用 `manage-*`
- 生成类统一使用 `generate-*`
- 应用类统一使用 `apply-*`
- 新写接口默认采用 `operations[]` 或 `actions[]`
- 新写接口默认支持 `dry_run`
- 多步修改优先支持事务、`save`，必要时支持 `rollback_on_error`
- 变更型工具统一复用 `Modify()`、`MarkPackageDirty()`、保存与编译辅助路径
- MCP-facing 错误继续统一使用 `UEBMCP_*` 前缀

## 6. 每一步收尾要求

每个阶段收尾时，至少做这些事：

1. 在注册入口中挂上新工具
2. 更新中英文 `Tools-Reference`
3. 必要时同步 `CapabilityMatrix` 与工具开发文档
4. 补至少一轮人工 smoke
5. 留下 provenance 说明，明确只参考能力面和黑盒体验，不复制实现细节
