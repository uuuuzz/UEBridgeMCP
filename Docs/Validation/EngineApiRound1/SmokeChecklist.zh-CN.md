# Engine API Round 1 Smoke Checklist

范围：Engine API Phase 4B v1。本轮覆盖本地 reflection search、class member summary、plugin capability summary 和 editor subsystem summary。

宿主项目：`G:\UEProjects\MyProject`

证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\EngineApiRound1\<timestamp>`

## Checklist

- 确认 `tools/list` 暴露 `query-engine-api-symbol`、`query-class-member-summary`、`query-plugin-capabilities`、`query-editor-subsystem-summary`。
- 用 `query-engine-api-symbol` 搜索已知 class，例如 `Actor`，确认返回 `/Script/Engine.Actor` 或 `Actor`。
- 用 `query-engine-api-symbol` 搜索已知 function，例如 `LineTrace`，确认返回 function symbols。
- 用 `query-class-member-summary` 查询 `Actor`，并用 `Location` 过滤成员，确认 function / property section 都是结构化返回。
- 用 `query-plugin-capabilities` 查询 `UEBridgeMCP`，确认返回插件与 module descriptors。
- 用 `query-plugin-capabilities` 搜索 `Niagara`，确认 ranked plugin results。
- 用 `query-editor-subsystem-summary` 查询 editor / engine families，确认返回 subsystem class 与 active-state 字段。
- 用不存在的 class name 调 `query-class-member-summary`，覆盖结构化负路径。

## 最新通过运行

- Timestamp: `20260423_220509`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\EngineApiRound1\20260423_220509`
- Runtime tool count: `140`

## 边界

- Phase 4B 只使用本地 Unreal reflection、plugin descriptor、module state 和 subsystem class / instance availability。
- 不抓取外部文档，不镜像在线文档，也不索引任意 source/config 文件。
- Reflection search 通过 `max_classes` 和 `limit` 限流，避免长时间阻塞 editor GameThread。
