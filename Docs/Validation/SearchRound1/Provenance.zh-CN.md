# Search Round 1 Provenance

日期：2026-04-23

阶段：Search Phase 4A v1

宿主项目：`G:\UEProjects\MyProject`

实现仓库：`G:\UEProjects\UEBridgeMCP`

## 实现

- 在核心 editor module 中新增 5 个 always-on Search 工具：
- `search-project`
- `search-assets-advanced`
- `search-blueprint-symbols`
- `search-level-entities`
- `search-content-by-class`
- 新增共享 helper：`Source/UEBridgeMCPEditor/Private/Tools/Search/SearchToolUtils.*`，负责 ranked fuzzy/camel-case scoring、path filters、asset registry collection、Blueprint symbol scanning 和 level actor serialization。
- 在 `RegisterBuiltInTools()` 中与现有 project / asset / reference query 工具一起注册。
- 复用现有 actor handle/session serialization 与 world resolution helper，不引入单独的 search index service。

## 验证

- Build：`MyProjectEditor Win64 Development` 使用 `-MaxParallelActions=4` 构建通过。
- `tools/list` runtime tool count：`136`。
- 最终证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\SearchRound1\20260423_214405`
- 运行时创建的验证资产：
- `/Game/UEBridgeMCPValidation/SearchRound1/BP_SearchRound1_20260423_214405`
- 临时 editor-world actor label：
- `UEBridgeMCP_SearchActor_20260423_214405`

## 覆盖回归

- 5 个 Search Phase 4A 工具均在 `tools/list` 可见。
- `create-blueprint-pattern` 仍能创建可搜索 Blueprint fixture。
- `search-assets-advanced` 返回 ranked asset registry matches。
- `search-content-by-class` 返回 class-filtered Blueprint content，并在缺失 `class` 时返回结构化 `UEBMCP_MISSING_REQUIRED_ARGUMENT`。
- `search-blueprint-symbols` 返回已生成 Blueprint function 和 custom event node symbol。
- `search-level-entities` 返回 editor-world actor handle、transform、bounds 等信息。
- `search-project` 聚合 asset、Blueprint symbol、level entity 三个 section，并返回 flattened ranked results。

## 边界

- 本轮不建立持久搜索索引。
- 本轮不搜索 C++ source、config text、logs 或任意项目文件全文。
- 本轮不做语义级 Blueprint graph analysis，只返回 symbols 和可选 node metadata。
- 除 smoke fixture setup/cleanup 外，本轮工具是只读查询能力。
