# Audio Round 1 Provenance

Purpose:

- Record the first Audio Phase 2B implementation and smoke evidence.
- Keep the boundary explicit: this round is not MetaSound, SoundClass graph authoring, or arbitrary SoundCue graph editing.

Implementation provenance:

- Audio tools are core-editor always-on tools registered from `RegisterBuiltInTools()`.
- The editor module now depends on `AudioEditor` so SoundCue creation can use the engine SoundCue factory.
- Shared Audio behavior is centralized in `Source/UEBridgeMCPEditor/Private/Tools/Audio/AudioToolUtils.*`.
- Tool entrypoints are `query-audio-asset-summary`, `create-sound-cue`, `edit-sound-cue-routing`, `create-audio-component-setup`, and `apply-audio-to-actor`.
- Editor-world `play_now` is intentionally deferred to avoid starting audio playback directly on the editor GameThread during smoke.

Validation provenance:

- Host: `G:\UEProjects\MyProject`.
- Asset root: `/Game/UEBridgeMCPValidation/AudioRound1`.
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\AudioRound1`.
- Final smoke evidence for this implementation is `G:\UEProjects\UEBridgeMCP\Tmp\Validation\AudioRound1\20260423_185956`.
- Runtime-created SoundCue assets use timestamped names such as `SC_AudioRound1_<timestamp>`.
- The final smoke targeted the existing editor-world actor `SM_SkySphere` with a temporary component named `AudioRound1Component_<timestamp>` and cleaned that component afterward with `run-python-script`.

Known v1 boundaries:

- SoundCue routing v1 supports wave-player, random, mixer, attenuation wrapping, multiplier edits, attenuation asset assignment, override flag edits, and clear routing.
- SoundCue graph layout and arbitrary low-level node graph editing remain out of scope.
- AudioComponent tools focus on editor-world component setup and application. Runtime/PIE playback validation can be added later.
- MetaSound is covered separately by MetaSound Round 1 / Phase 2C validation.
- `edit-level-batch` actor spawning was not used in final Audio smoke because a prior validation attempt exposed an editor-world hang on `spawn_actor`; final assertions use public Audio tools plus a small cleanup script.
