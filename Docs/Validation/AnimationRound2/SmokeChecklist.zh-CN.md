# Animation Round 2 Smoke Checklist

目标：关闭 `edit-anim-blueprint-state-machine` 的历史正向 smoke 缺口，同时不向仓库提交 `.uasset` fixture。

宿主：

- Project：`G:\UEProjects\MyProject`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\AnimationRound2\20260423_223834`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/AnimationRound2`

Checklist：

- 确认 `tools/list` 包含 `create-asset`、`edit-anim-blueprint-state-machine`、`query-animation-asset-summary`、`compile-assets`。
- 查询 Step 6 现有验证 AnimBlueprint，复用它的 target skeleton path 创建运行时 fixture。
- 用 `create-asset(asset_class="AnimBlueprint")` 创建新 AnimBlueprint，并把 `parent_class` 设为 Skeleton 资产路径。
- 确认创建结果返回 `created_class="AnimBlueprint"`，而不是普通 `Blueprint`。
- 调用 `edit-anim-blueprint-state-machine`，覆盖 `create_state_machine`、`add_state`、`set_entry_state`、`add_transition`、`set_state_sequence`。
- 查询新 AnimBlueprint 摘要，确认存在一个 `Locomotion` state machine，包含 2 个 state 和 1 条 transition。
- 用 `compile-assets` 编译并保存 AnimBlueprint，确认 compile error 为 0。
- 对缺失 state machine 执行负路径，确认返回结构化失败。

最终证据：

- `summary.json`：核心验证标志全部通过。
- `create-animblueprint.json`：AnimBlueprint factory 路径返回 `created_class="AnimBlueprint"`。
- `edit-state-machine-create.json`：6 个 state-machine 操作全部成功。
- `query-animation-summary.json`：`state_machine_count=1`、`state_count=2`、`transition_count=1`。
- `compile-animblueprint.json`：编译成功，`error_count=0`。
