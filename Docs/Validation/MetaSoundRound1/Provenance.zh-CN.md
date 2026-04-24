# MetaSound Round 1 Provenance

目的：

- 记录第一轮 MetaSound Phase 2C 实现与 smoke 证据。
- 明确边界：本轮不是运行时播放验证、任意 DSP 图生成，也不是更深的 MetaSound 资产迁移工具。

实现来源：

- MetaSound 工具是核心编辑器条件工具，只有在引擎 MetaSound 模块可用时才会从 `RegisterBuiltInTools()` 注册。
- 编辑器模块现在依赖 `MetasoundEngine`、`MetasoundFrontend`、`MetasoundEditor`。
- 共享 MetaSound 行为集中在 `Source/UEBridgeMCPEditor/Private/Tools/MetaSound/MetaSoundToolUtils.*`。
- 工具入口为 `query-metasound-summary`、`create-metasound-source`、`edit-metasound-graph`、`set-metasound-input-defaults`。
- v1 使用官方 engine builder API 创建 Source 和编辑已有 Source 图，并有意把图编辑限制在显式结构性操作。

验证来源：

- 宿主：`G:\UEProjects\MyProject`。
- 资产根目录：`/Game/UEBridgeMCPValidation/MetaSoundRound1`。
- 证据根目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\MetaSoundRound1`。
- 本轮最终 smoke 证据为 `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MetaSoundRound1\20260423_192531`。
- 运行时创建的 MetaSound Source 使用 `MS_MetaSoundRound1_<timestamp>` 这类带时间戳命名。
- 最终 smoke 创建了 `/Game/UEBridgeMCPValidation/MetaSoundRound1/MS_MetaSoundRound1_20260423_192531`。
- 同一证据目录还包含 post-patch optional 字段检查，资产为 `/Game/UEBridgeMCPValidation/MetaSoundRound1/MS_MetaSoundRound1_Optional_20260423_192531`。

已知 v1 边界：

- v1 支持的 literal default 为 bool、int32、float 和 string；trigger/audio graph input 只做结构接入，不承诺语义默认值。
- `edit-metasound-graph` 暴露按 class name 插入节点和显式连接操作，但 smoke 只覆盖 graph I/O 与 layout，避免依赖不稳定的引擎 class-name fixture。
- 运行时音频播放和 Actor component 应用继续归 Audio 工具族负责。
- 任意 MetaSound 图生成、模板 catalog、preset 迁移和 DSP 级验证仍留给后续。
