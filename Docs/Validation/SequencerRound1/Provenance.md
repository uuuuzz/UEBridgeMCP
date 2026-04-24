# Sequencer Round 1 Provenance

This round was added after the broader authoring roadmap exposed that Sequencer had a write-only minimal surface but no self-contained create/query validation loop.

Implementation changes:

- `create-asset` now supports `asset_class="LevelSequence"` and initializes the asset through `ULevelSequence::Initialize()`, ensuring a real MovieScene exists before edits run.
- `query-level-sequence-summary` was added as a conditional Sequencer query tool for playback range, frame rates, bindings, possessables, spawnables, tracks, sections, and Camera Cut details.
- `edit-sequencer-tracks` now creates Camera Cuts through the engine `AddCameraCutTrack(...)` path, so `GetCameraCutTrack()` is populated.
- `query-level-sequence-summary` still includes a compatibility fallback for old assets where earlier tool builds accidentally created Camera Cut tracks as ordinary master tracks.
- Binding display-name writes no longer use deprecated `FMovieSceneBinding::SetName`; they write through possessable/spawnable entries instead.

Validation run:

- Timestamp: `20260423_225804`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\SequencerRound1\20260423_225804`
- Created asset: `/Game/UEBridgeMCPValidation/SequencerRound1/LS_SequencerRound1_20260423_225804`
- Bound actor: `SM_SkySphere`

Build:

- Command: `Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- Result: succeeded.

Scope boundaries:

- This round only covers Level Sequence asset creation, summary query, actor binding, basic tracks/keys, playback range, and Camera Cut creation.
- It does not add shot/sequence hierarchy authoring, spawnable creation, cinematic camera creation, Control Rig sequencing, audio tracks, render queue integration, or timeline UI automation.
