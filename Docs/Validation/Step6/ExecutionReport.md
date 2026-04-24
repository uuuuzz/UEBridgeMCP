# Step 6 Execution Report

## Goal

Close Step 6 as a validation-ready delivery in `G:\UEProjects\MyProject`, keeping `UEBridgeMCP` and `UnrealMCPServer` mounted side by side and using the baseline server for black-box protocol comparison.

## Runtime evidence

- Output root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6`
- Final accepted run: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727`
- Summary file: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727\summary.json`
- Scenario file: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727\scenario-results.json`

## Environment notes

- The project configuration remains `127.0.0.1:8080` for `UEBridgeMCP` and `127.0.0.1:13579` for `UnrealMCPServer`.
- The final validation run executed `UEBridgeMCP` temporarily on `127.0.0.1:18080` because an unrelated `LyraStarterGame` editor session was already listening on `8080`.
- `G:\UEProjects\MyProject\Config\DefaultUEBridgeMCP.ini` was restored to `8080` after each validation run.
- Final run editor process exited cleanly: `Editor closed: True`.

## Protocol

- `UnrealMCPServer`:
  - `initialize` succeeded
  - `tools/list` succeeded
  - runtime tool count from `tools/list`: `304`
- `UEBridgeMCP`:
  - `initialize` succeeded
  - `tools/list` succeeded
  - `registeredCount`: `93`
  - runtime tool count from `tools/list`: `93`
  - count match: `true`

The protocol blocker conditions are clear in the final run:

- no handshake failure
- no `registeredCount` mismatch
- no unreadable `resources/*` or `prompts/*`

## Resources / Prompts / Presets

Both servers passed MCP resources and prompts reads:

- Baseline resource read: `unreal://project/info`
- UEBridge resource read: `uebmcp://builtin/resources/performance-triage-guide`
- Baseline prompt get: `world_builder`
- UEBridge prompt get: `performance-triage-brief`

UEBridgeMCP preset chain passed end to end:

- `manage-workflow-presets` `upsert_preset`: passed
- `manage-workflow-presets` `list_presets`: passed
- `run-workflow-preset` `dry_run=true`: passed
- `run-workflow-preset` live execution: passed
- `manage-workflow-presets` `delete_preset`: passed

The live preset run is especially important because it proved the whole Step 6 content path together:

- resolved built-in resource: `uebmcp://builtin/resources/performance-triage-guide`
- expanded prompt: `performance-triage-brief`
- executed tool call: `query-performance-report`
- returned structured report data for `L_Empty`

## Animation

The validation harness duplicated minimal fixtures under `/Game/UEBridgeMCPValidation/Step6/Animations` and ran the animation path successfully for:

- `query-animation-asset-summary` on the duplicated sequence
- `query-animation-asset-summary` on the duplicated AnimBP
- `create-animation-montage`
- `query-animation-asset-summary` on the created montage

Positive results from the final run:

- duplicated sequence: `VLD_Tutorial_Idle`
- duplicated AnimBP: `VLD_TutorialTPP_AnimBlueprint`
- created montage: `/Game/UEBridgeMCPValidation/Step6/Animations/VLD_TutorialMontage`
- montage query showed:
  - `asset_type=AnimMontage`
  - `montage_section_count=1`
  - `montage_sections=["Default"]`

Remaining gap:

- the duplicated tutorial AnimBP reported `state_machine_count=0`
- because of that, `edit-anim-blueprint-state-machine` could not be exercised on a positive path without introducing an additional mounted fixture source

## Performance

The performance tools passed on the editor world:

- `query-performance-report`: passed
- `capture-performance-snapshot`: passed

Snapshot evidence from the final run:

- snapshot directory: `../../../../../UEProjects/MyProject/Saved/UEBridgeMCP/PerformanceSnapshots/20260423_114753`
- report path: `../../../../../UEProjects/MyProject/Saved/UEBridgeMCP/PerformanceSnapshots/20260423_114753/report.json`
- viewport artifact: `viewport.png`

This satisfies the Step 6 requirement that performance output be structured, persisted, and stable enough for comparison.

## World Production

The world production path passed for the always-on tool:

- `edit-level-batch` spawned `VLD_SplineActor` with `ValidationSpline`
- `edit-spline-actors` passed

The spline edit result confirmed:

- point count changed from `2 -> 3`
- modified asset: `/Game/Maps/L_Empty`

World Partition negative-path behavior also passed:

- `query-worldpartition-cells`
- `world_name=L_Empty`
- `world_partition_enabled=false`
- `cell_count=0`

This is the expected non-WP result on `L_Empty`.

## Conditional Capabilities

`get-project-info.optional_capabilities` from the final run reported:

- `sequencer_available=true`
- `landscape_available=true`
- `foliage_available=true`
- `control_rig_available=true`
- `pcg_available=true`
- `external_ai_available=true`
- `world_partition_available=false`

Tool visibility from the final run reported:

- `edit-sequencer-tracks=true`
- `edit-landscape-region=true`
- `edit-foliage-batch=true`
- `edit-control-rig-graph=true`
- `generate-pcg-scatter=true`
- `generate-external-content=true`
- `query-worldpartition-cells=true`

Interpretation:

- feature-driven optional tools were registered as expected
- the `world_partition_available=false` signal reflects the current map state, not the absence of the query tool itself
- `query-worldpartition-cells` remaining available on a non-WP map is acceptable because the tool returned structured no-op style data instead of crashing

## External AI

Final-run negative-path validation passed:

- unconfigured request returned `UEBMCP_EXTERNAL_AI_NOT_CONFIGURED`
- bad endpoint settings returned `UEBMCP_EXTERNAL_AI_TIMEOUT`

Important implementation note:

- earlier validation runs exposed a crash after timeout in the External AI provider callback path
- that bug was fixed before the final accepted run
- the final accepted run exited cleanly, and `preset_delete` also completed afterward, confirming the crash no longer interrupted the harness

The success-path case remains credential-dependent and was not treated as a code failure.

## Fixes applied during validation closure

Two real blockers were found and fixed while closing this validation loop:

1. Tool registry shutdown safety
- Optional modules could hit invalid tool-instance handles while unregistering during editor shutdown.
- The registry now stores lazy tool instances as `TWeakObjectPtr` and removes stale entries safely before reuse or unregister.

2. External AI timeout callback safety
- The bad-config timeout path previously allowed an HTTP completion callback to touch freed stack state after `Generate()` had already returned.
- The provider now uses shared request state so late completion callbacks cannot dereference invalid stack locals or returned sync events.

Both fixes are included in the final accepted run.

## Conclusion

The final accepted run is blocker-free for the Step 6 validation closure goals in `MyProject`:

- protocol and tool-count checks passed
- resources/prompts are readable
- preset chain is intact
- performance snapshot/report path passed
- spline and World Partition negative-path checks passed
- External AI negative paths fail cleanly
- editor shutdown is clean

One non-blocking validation gap remains:

- `edit-anim-blueprint-state-machine` positive-path smoke was not executed because the mounted validation AnimBP fixture has no state machine in this host project

For the current host-project setup, Step 6 is validation-ready and delivery-grade, with one explicit fixture-coverage gap tracked separately in `Issues.md`.
