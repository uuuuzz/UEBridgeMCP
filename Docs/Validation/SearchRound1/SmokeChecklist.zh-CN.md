# Search Round 1 Smoke Checklist

范围：Search Phase 4A v1。本轮覆盖 ranked asset search、class-first content search、Blueprint symbol search、level entity search 和 unified project search。

宿主项目：`G:\UEProjects\MyProject`

验证资产根目录：`/Game/UEBridgeMCPValidation/SearchRound1`

证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\SearchRound1\<timestamp>`

## Checklist

- 确认 `tools/list` 暴露 `search-project`、`search-assets-advanced`、`search-blueprint-symbols`、`search-level-entities`、`search-content-by-class`。
- 使用 `create-blueprint-pattern` 在验证根目录下创建带时间戳的 Actor Blueprint fixture。
- 用 `search-assets-advanced` 搜索验证根目录，确认能返回 fixture asset 和 ranked score。
- 用 `search-content-by-class` 设置 `class=Blueprint`，确认能返回同一个 fixture。
- 用 `search-blueprint-symbols` 搜索已生成函数，确认返回 function symbol。
- 用 `search-blueprint-symbols include_nodes=true` 搜索已生成 custom event，确认返回 node symbol 和 `node_guid`。
- 运行时创建一个临时 editor-world StaticMeshActor，并写入时间戳 label 和 tag。
- 用 `search-level-entities` 搜索 editor world，确认返回 actor label、handle、tag、transform 和 bounds。
- 用 `search-project` 同时开启三个 section，确认 assets、Blueprint symbols 和 level entities 都有结果。
- 用必然不匹配的 query 覆盖 empty result 路径。
- 省略 `search-content-by-class.class`，覆盖结构化负路径。
- 清理临时 editor-world actor。

## 最新通过运行

- Timestamp: `20260423_214405`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\SearchRound1\20260423_214405`
- Fixture asset: `/Game/UEBridgeMCPValidation/SearchRound1/BP_SearchRound1_20260423_214405`
- Temporary actor label: `UEBridgeMCP_SearchActor_20260423_214405`

## 边界

- Phase 4A 搜索 Unreal editor objects 和 asset registry metadata，不新增 C++/config/source full-text index。
- Blueprint symbol search 会加载 Blueprint，因此通过 `max_blueprints` 限流保护 GameThread。
- `search-project` 返回 per-section arrays，同时提供 flattened ranked `results[]`。
