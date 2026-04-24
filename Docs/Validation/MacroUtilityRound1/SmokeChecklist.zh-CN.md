# Macro / Utility Round 1 Smoke Checklist

范围：Phase 4C v1。本轮覆盖 curated editor macro、高层 Blueprint / Level pattern、项目维护检查和 workspace health 快照。

宿主工程：`G:\UEProjects\MyProject`

验证资产根目录：`/Game/UEBridgeMCPValidation/MacroUtilityRound1`

证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\MacroUtilityRound1\<timestamp>`

## Checklist

- 确认 `tools/list` 暴露 `run-editor-macro`、`generate-blueprint-pattern`、`generate-level-pattern`、`run-project-maintenance-checks`、`query-workspace-health`。
- 运行 `query-workspace-health`，确认返回 project paths、server state、editor world state、optional capabilities 和 dirty packages。
- 以 dry-run 运行 `run-project-maintenance-checks`，确认 workspace health 与 unused asset checks 都进入结构化 `checks[]`。
- 以 `dry_run=true` 运行 `generate-blueprint-pattern`，确认返回委托给 `create-blueprint-pattern` 的计划。
- 用 `generate-blueprint-pattern` 在验证目录下创建带时间戳的 Actor Blueprint，并启用 final compile / save。
- 以 `dry_run=true` 运行 `generate-level-pattern` 的 `test_anchor_pair`，确认 planned actor labels / transforms。
- 真实运行 `generate-level-pattern` 的 `test_anchor_pair`，确认 editor world 中生成 actor 并返回结果。
- 运行 `run-editor-macro` 的 `collect_workspace_health`。
- 运行 `run-editor-macro` 的 `compile_blueprint_assets` dry-run 与真实编译路径。
- 运行带 `compile_asset_paths` 的 `run-project-maintenance-checks`，验证真实 Blueprint compile check。
- 运行 `run-editor-macro` 的 `cleanup_generated_actors` dry-run 与真实清理路径，移除临时 level actors。
- 覆盖 unsupported editor macro 与 unsupported level pattern 的结构化负路径。

## Latest Passing Run

- Timestamp: `20260423_222221`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MacroUtilityRound1\20260423_222221`
- Generated Blueprint: `/Game/UEBridgeMCPValidation/MacroUtilityRound1/BP_GeneratedPattern_20260423_222221`
- Temporary level actor prefix: `UEBMCP_MacroUtility_20260423_222221`
- Runtime tool count: `145`

## Notes

- `run-editor-macro` 是 curated macro 入口，不接受任意脚本文本。
- `generate-blueprint-pattern` 复用已验证过的 `create-blueprint-pattern`，不新建第二套 Blueprint pattern backend。
- `generate-level-pattern` 使用 engine-only StaticMeshActor cube scaffold，不依赖项目内容。
