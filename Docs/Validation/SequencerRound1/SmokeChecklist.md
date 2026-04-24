# Sequencer Round 1 Smoke Checklist

Purpose: close a self-contained Sequencer authoring loop with runtime-created Level Sequence assets and no repo-tracked `.uasset` fixtures.

Host:

- Project: `G:\UEProjects\MyProject`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\SequencerRound1\20260423_225804`
- Validation asset root: `/Game/UEBridgeMCPValidation/SequencerRound1`

Checklist:

- Confirm `tools/list` contains `query-level-sequence-summary`, `edit-sequencer-tracks`, `create-asset`, and `search-level-entities`.
- Confirm `get-project-info.optional_capabilities.sequencer_available=true`.
- Resolve an editor-world actor through `search-level-entities`; the final smoke used `SM_SkySphere`.
- Create a new Level Sequence through `create-asset(asset_class="LevelSequence")`.
- Query the empty Level Sequence and confirm it already has a MovieScene.
- Run `edit-sequencer-tracks` with `dry_run=true` and confirm no assets are modified.
- Apply Sequencer edits: playback range, actor binding, transform track, two transform keys, bool property track/key, and Camera Cut.
- Query the edited Level Sequence and confirm binding, track, section, playback range, and Camera Cut counts.
- Confirm Camera Cut is represented through the dedicated sequence camera-cut track, not as an ordinary master track fallback.
- Query a missing Level Sequence path and confirm the structured `UEBMCP_ASSET_NOT_FOUND` negative path.

Final evidence:

- `summary.json`: all validation flags passed.
- `create-level-sequence.json`: `create-asset` returned `created_class="LevelSequence"`.
- `query-empty-sequence.json`: new asset reported an initialized MovieScene.
- `edit-sequencer-dry-run.json`: dry-run produced no `modified_assets`.
- `edit-sequencer-apply.json`: all eight Sequencer operations succeeded.
- `query-edited-sequence.json`: `binding_count=1`, `track_count=3`, `section_count=5`, `camera_cut_count=1`.
- `negative-missing-sequence.json`: structured missing-asset failure returned `UEBMCP_ASSET_NOT_FOUND`.
