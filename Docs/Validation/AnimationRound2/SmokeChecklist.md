# Animation Round 2 Smoke Checklist

Purpose: close the historical positive smoke gap for `edit-anim-blueprint-state-machine` without adding repo-tracked `.uasset` fixtures.

Host:

- Project: `G:\UEProjects\MyProject`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\AnimationRound2\20260423_223834`
- Validation asset root: `/Game/UEBridgeMCPValidation/AnimationRound2`

Checklist:

- Confirm `tools/list` contains `create-asset`, `edit-anim-blueprint-state-machine`, `query-animation-asset-summary`, and `compile-assets`.
- Query the existing Step 6 validation AnimBlueprint and reuse its target skeleton path for runtime-created fixtures.
- Create a new AnimBlueprint with `create-asset(asset_class="AnimBlueprint")` and `parent_class` set to the skeleton asset path.
- Verify the created asset reports `created_class="AnimBlueprint"` instead of a generic `Blueprint`.
- Run `edit-anim-blueprint-state-machine` with `create_state_machine`, `add_state`, `set_entry_state`, `add_transition`, and `set_state_sequence`.
- Query the new AnimBlueprint summary and confirm one `Locomotion` state machine with two states and one transition.
- Compile/save the AnimBlueprint with `compile-assets` and confirm zero compile errors.
- Run a missing-state-machine negative path and confirm a structured failure.

Final evidence:

- `summary.json`: all core validation flags passed.
- `create-animblueprint.json`: AnimBlueprint factory path returned `created_class="AnimBlueprint"`.
- `edit-state-machine-create.json`: all six state-machine operations succeeded.
- `query-animation-summary.json`: `state_machine_count=1`, `state_count=2`, `transition_count=1`.
- `compile-animblueprint.json`: compile succeeded with `error_count=0`.
