# Landscape Round 1 Smoke Checklist

目的：关闭一条不依赖现有关卡 fixture 的 Landscape 创建、查询、编辑自包含验证闭环。

宿主：

- 工程：`G:\UEProjects\MyProject`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\LandscapeRound1\20260423_235345`
- 验证 Actor label：`LandscapeRound1_20260423_235345`

Checklist：

- 确认 `tools/list` 包含 `query-landscape-summary`、`create-landscape`、`edit-landscape-region`、`edit-level-batch` 和 `get-project-info`。
- 确认 `get-project-info.optional_capabilities.landscape_available=true`。
- 用 `query-landscape-summary` 查询 editor world，记录基线。
- 用 `create-landscape` 执行 `dry_run=true`，确认没有修改资产。
- 用 `create-landscape` 正式创建 1x1 component、1 section per component、7 quads per section 的小型 Landscape。
- 用 `query-landscape-summary(include_components=true, sample_points=[[1,1]])` 查询新 Landscape，确认 1 个 Landscape、1 个 component、8x8 resolution，初始 local height 为 `0`。
- 用 `edit-landscape-region` 对 `[0,0] -> [2,2]` 区域执行 `dry_run=true` 和 `delta=128`，确认没有修改资产且高度仍为 `0`。
- 用同样 region 和 delta 正式执行 `edit-landscape-region`。
- 查询 `[1,1]` 与 `[4,4]` 高度采样，确认区域内高度从 `0` 变为 `128`，区域外仍为 `0`。
- 查询缺失 Landscape actor，确认结构化返回 `UEBMCP_LANDSCAPE_NOT_FOUND`。
- 通过 `edit-level-batch(delete_actor)` 删除临时 Landscape，并确认后续按 actor name 查询失败。

最终证据：

- `summary.json`：全部 validation flag 通过。
- `create-landscape-dry-run.json`：dry-run 成功且没有 `modified_assets`。
- `create-landscape.json`：创建了 1 component、8x8 resolution 的 Landscape。
- `query-created.json`：返回新 Landscape、component 详情和初始高度采样。
- `edit-landscape-dry-run.json`：dry-run 成功且没有高度变化。
- `edit-landscape-apply.json`：region edit 返回 `changed=true`。
- `query-after-apply.json`：区域内高度采样为 `128`，区域外采样仍为 `0`。
- `negative-missing-landscape.json`：缺失 Landscape 返回结构化失败。
- `cleanup-delete-landscape.json`：临时 Landscape actor 清理成功。
