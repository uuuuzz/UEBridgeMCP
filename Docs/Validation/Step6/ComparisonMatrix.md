# Step 6 Comparison Matrix

This document records the behavior-level Step 6 validation comparison in `G:\UEProjects\MyProject`, using `UnrealMCPServer` as the black-box baseline and `UEBridgeMCP` as the target.

Allowed status values:

- `Equivalent`
- `Intentional deviation`
- `Missing`
- `Bug`

## Scope

- Host project: `G:\UEProjects\MyProject`
- Validation map: `/Game/Maps/L_Empty`
- Temporary validation content root: `/Game/UEBridgeMCPValidation/Step6`
- Baseline plugin: `UnrealMCPServer`
- Target plugin: `UEBridgeMCP`
- Final validated run: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727`

## Matrix

| Scenario | UnrealMCPServer baseline | UEBridgeMCP result | Status | Notes |
| --- | --- | --- | --- | --- |
| Protocol handshake (`initialize`, `tools/list`) | `initialize` and `tools/list` both returned `200`; `tools/list` reported `304` tools | `initialize` and `tools/list` both returned `200`; `registeredCount=93` and `tools/list` also returned `93` tools | `Equivalent` | Baseline does not expose `registeredCount` in `initialize`; UEBridgeMCP does, and its value matches runtime tool registration. |
| Resources list/read | `resources/list` and `resources/read` succeeded; validation read `unreal://project/info` | `resources/list` and `resources/read` succeeded; validation read `uebmcp://builtin/resources/performance-triage-guide` | `Equivalent` | Both servers expose working MCP resources. The resource payloads differ by design. |
| Prompts list/get | `prompts/list` and `prompts/get` succeeded; validation expanded `world_builder` | `prompts/list` and `prompts/get` succeeded; validation expanded `performance-triage-brief` | `Equivalent` | Both servers expose working MCP prompts. Prompt catalogs differ by design. |
| Workflow presets | No equivalent preset management or preset runner surface in the baseline tool set | `manage-workflow-presets` and `run-workflow-preset` passed `upsert -> list -> dry_run -> live -> delete` | `Intentional deviation` | UEBridgeMCP adds a project-level preset workflow layer; this is deliberate extra scope, not a parity regression. |
| Animation summary | Baseline exposes overlapping animation inspection surfaces such as `list_animation_assets`, `get_anim_blueprint_info`, and `get_anim_montage_info` | `query-animation-asset-summary` returned compact summaries for sequence, montage, and AnimBP assets | `Intentional deviation` | UEBridgeMCP intentionally collapses several read flows into one summary tool. |
| Animation montage creation | Baseline exposes `create_anim_montage` | `create-animation-montage` successfully created `/Game/UEBridgeMCPValidation/Step6/Animations/VLD_TutorialMontage` and `query-animation-asset-summary` read it back | `Equivalent` | Capability intent matches even though schema and envelope differ. |
| Anim Blueprint state-machine edit | Baseline exposes state-machine editing primitives such as `create_anim_state_machine`, `add_anim_state`, and `add_anim_transition` | UEBridgeMCP tool exists, but the validation AnimBP fixture reported `state_machine_count=0`, so the positive-path edit was not exercised in this host project | `Missing` | This is a validation-fixture gap, not an implementation crash. See `Issues.md`. |
| Performance report | Baseline exposes overlapping performance reads via `get_render_stats`, `get_memory_report`, and `profile_actors_in_view` | `query-performance-report` succeeded and returned FPS, frame time, actor/object counts, and memory summary for `L_Empty` | `Intentional deviation` | UEBridgeMCP intentionally consolidates several baseline-style reads into one summary report. |
| Performance snapshot | No equivalent baseline snapshot envelope combining report, viewport artifact, and log slice | `capture-performance-snapshot` succeeded and wrote `report.json` plus `viewport.png` under `Saved/UEBridgeMCP/PerformanceSnapshots/20260423_114753` | `Intentional deviation` | Extra capability beyond the baseline. |
| Spline editing | Baseline exposes `create_spline_actor`, `add_spline_point`, `set_spline_point`, `remove_spline_point`, `get_spline_info`, and `set_spline_closed` | `edit-spline-actors` successfully spawned and edited `VLD_SplineActor` and read back point counts in structured results | `Equivalent` | Capability intent matches; UEBridgeMCP uses a batch-first write shape. |
| World Partition negative path on `L_Empty` | Baseline exposes `get_world_partition_info` for both WP and non-WP maps | `query-worldpartition-cells` returned `supported=true`, `world_partition_enabled=false`, and `cell_count=0` without error | `Equivalent` | Negative-path behavior on a non-WP map is correct. |
| Conditional capability registration | Baseline does not expose an `optional_capabilities` summary | `tools/list` included conditional tools for loaded editor features; `get-project-info.optional_capabilities` reported `sequencer/landscape/foliage/control_rig/pcg/external_ai=true` and `world_partition_available=false` | `Intentional deviation` | UEBridgeMCP distinguishes tool availability from current-map WP state. `query-worldpartition-cells` remaining callable on a non-WP world is acceptable because it returns structured no-op data. |
| External AI unconfigured path | No equivalent baseline surface | `generate-external-content` returned structured `UEBMCP_EXTERNAL_AI_NOT_CONFIGURED` | `Intentional deviation` | Negative-path contract is UEBridgeMCP-specific. |
| External AI bad-config path | No equivalent baseline surface | `generate-external-content` returned structured `UEBMCP_EXTERNAL_AI_TIMEOUT` on bad endpoint settings | `Intentional deviation` | Timeout path now fails cleanly without crashing the editor. |

## Summary

- Final run evidence shows the protocol layer, resources/prompts, preset chain, performance tools, spline editing, World Partition negative path, and External AI negative paths all working in `MyProject`.
- No `Bug` rows remain in the final matrix.
- One `Missing` row remains for `edit-anim-blueprint-state-machine`, caused by the currently mounted validation fixture lacking a state machine in this host project.
