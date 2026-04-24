# Sequencer Round 1 Smoke Checklist

目标：关闭一个自包含的 Sequencer 作者工作流，运行时创建 Level Sequence 资产，不向仓库提交 `.uasset` fixture。

宿主：

- Project：`G:\UEProjects\MyProject`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\SequencerRound1\20260423_225804`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/SequencerRound1`

Checklist：

- 确认 `tools/list` 包含 `query-level-sequence-summary`、`edit-sequencer-tracks`、`create-asset`、`search-level-entities`。
- 确认 `get-project-info.optional_capabilities.sequencer_available=true`。
- 通过 `search-level-entities` 解析一个 editor-world Actor；最终 smoke 使用 `SM_SkySphere`。
- 通过 `create-asset(asset_class="LevelSequence")` 创建新的 Level Sequence。
- 查询空 Level Sequence，确认它已经有 MovieScene。
- 以 `dry_run=true` 调用 `edit-sequencer-tracks`，确认没有修改资产。
- 正式应用 Sequencer 编辑：playback range、Actor binding、transform track、两个 transform key、bool property track/key、Camera Cut。
- 查询编辑后的 Level Sequence，确认 binding、track、section、playback range 和 Camera Cut 数量。
- 确认 Camera Cut 通过专用 sequence camera-cut track 表示，而不是普通 master track fallback。
- 查询缺失 Level Sequence 路径，确认返回结构化 `UEBMCP_ASSET_NOT_FOUND` 负路径。

最终证据：

- `summary.json`：全部 validation flag 通过。
- `create-level-sequence.json`：`create-asset` 返回 `created_class="LevelSequence"`。
- `query-empty-sequence.json`：新资产已拥有初始化后的 MovieScene。
- `edit-sequencer-dry-run.json`：dry-run 没有产生 `modified_assets`。
- `edit-sequencer-apply.json`：8 个 Sequencer 操作全部成功。
- `query-edited-sequence.json`：`binding_count=1`、`track_count=3`、`section_count=5`、`camera_cut_count=1`。
- `negative-missing-sequence.json`：缺失资产结构化失败返回 `UEBMCP_ASSET_NOT_FOUND`。
