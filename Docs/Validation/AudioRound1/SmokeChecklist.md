# Audio Round 1 Smoke Checklist

Scope:

- Covers Audio Phase 2B only.
- Does not validate MetaSound, audio mixer runtime profiling, SoundClass graph authoring, or arbitrary SoundCue graph editing beyond the supported v1 operations.
- Host project is `G:\UEProjects\MyProject`.
- Validation assets live under `/Game/UEBridgeMCPValidation/AudioRound1`.
- Evidence lives under `G:\UEProjects\UEBridgeMCP\Tmp\Validation\AudioRound1\<timestamp>`.

Checklist:

- `tools/list` shows all five Audio Phase 2B tools: `query-audio-asset-summary`, `create-sound-cue`, `edit-sound-cue-routing`, `create-audio-component-setup`, and `apply-audio-to-actor`.
- `query-audio-asset-summary` returns a valid `SoundWave` summary for an engine fixture such as `/Engine/EngineSounds/WhiteNoise.WhiteNoise`.
- `create-sound-cue` creates a timestamped SoundCue under `/Game/UEBridgeMCPValidation/AudioRound1`, seeded with `initial_sound_wave_path`, and saves it.
- `query-audio-asset-summary` returns the created SoundCue summary and its root SoundCue node structure.
- `edit-sound-cue-routing` covers `dry_run=true`, `set_random_waves`, `wrap_with_attenuation`, `set_volume_multiplier`, save, and at least one structured failure with `rollback_on_error=true`.
- `create-audio-component-setup` covers both dry-run and positive editor-world actor setup paths with `save=false`.
- `apply-audio-to-actor` covers both dry-run and positive editor-world apply paths, including `play_now=true` returning `playback_deferred=true` instead of starting playback on the editor GameThread.
- At least one negative actor/audio path returns a structured error, such as a missing SoundCue path.
- Temporary actor components added during smoke are cleaned up or left unsaved.

Evidence expectations:

- Save raw MCP responses for initialization, tool visibility, SoundWave query, SoundCue create/query/edit, actor component setup/apply, and negative paths.
- Save a compact `summary.json` containing created asset paths, target actor/component names, success flags, deferred playback status, and structured error checks.
