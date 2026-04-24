# Engine API Round 1 Provenance

日期：2026-04-23

阶段：Engine API Phase 4B v1

宿主项目：`G:\UEProjects\MyProject`

实现仓库：`G:\UEProjects\UEBridgeMCP`

## 实现

- 新增 4 个 always-on 本地 Engine API 辅助工具：
- `query-engine-api-symbol`
- `query-class-member-summary`
- `query-plugin-capabilities`
- `query-editor-subsystem-summary`
- 新增共享 helper：`Source/UEBridgeMCPEditor/Private/Tools/Analysis/EngineApiToolUtils.*`，负责 class resolution、class/function/property/plugin serialization、flag summary、metadata extraction 和 ranked result trimming。
- 在 `RegisterBuiltInTools()` 中与已有 class hierarchy、search/reference 工具一起注册。
- 复用本地 Unreal reflection、`IPluginManager`、`FModuleManager` 和 subsystem API；不涉及外部文档查询。

## 验证

- Build：`MyProjectEditor Win64 Development` 使用 `-MaxParallelActions=4` 构建通过。
- `tools/list` runtime tool count：`140`。
- 最终证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\EngineApiRound1\20260423_220509`

## 覆盖回归

- 4 个 Engine API Phase 4B 工具均在 `tools/list` 可见。
- `query-engine-api-symbol` 返回 ranked class 和 function results。
- `query-class-member-summary` 能解析 `Actor` 并返回 property / function sections。
- `query-plugin-capabilities` 能解析 `UEBridgeMCP`，包含 module descriptors，并支持 ranked plugin query。
- `query-editor-subsystem-summary` 返回 editor / engine subsystem summary 和 active-state 字段。
- 缺失 class 查询返回结构化 `UEBMCP_CLASS_NOT_FOUND`。

## 边界

- 本轮不代理 Web 文档，也不需要网络访问。
- 本轮不创建持久 source-code / config text index。
- 本轮不修改 plugin enablement，不主动加载新 module，也不初始化 inactive subsystem。
