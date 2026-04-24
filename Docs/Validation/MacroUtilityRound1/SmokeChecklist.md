# Macro / Utility Round 1 Smoke Checklist

Scope: Phase 4C v1. This round covers curated editor macros, high-level Blueprint and level pattern wrappers, project maintenance checks, and workspace health snapshots.

Host project: `G:\UEProjects\MyProject`

Validation root: `/Game/UEBridgeMCPValidation/MacroUtilityRound1`

Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MacroUtilityRound1\<timestamp>`

## Checklist

- Confirm `tools/list` exposes `run-editor-macro`, `generate-blueprint-pattern`, `generate-level-pattern`, `run-project-maintenance-checks`, and `query-workspace-health`.
- Run `query-workspace-health` and verify project paths, server state, editor world state, optional capabilities, and dirty package data are returned.
- Run `run-project-maintenance-checks` in dry-run mode and verify workspace health plus unused-asset checks return structured `checks[]`.
- Run `generate-blueprint-pattern` with `dry_run=true` and verify it returns a delegated `create-blueprint-pattern` plan.
- Run `generate-blueprint-pattern` for a timestamped Actor Blueprint under the validation root, with final compile and save enabled.
- Run `generate-level-pattern` with `dry_run=true` for `test_anchor_pair` and verify planned actor labels/transforms.
- Run `generate-level-pattern` for `test_anchor_pair` in the editor world and verify spawned actors are returned.
- Run `run-editor-macro` with `collect_workspace_health`.
- Run `run-editor-macro` with `compile_blueprint_assets` in dry-run mode and real mode against the generated Blueprint.
- Run `run-project-maintenance-checks` with `compile_asset_paths` against the generated Blueprint.
- Run `run-editor-macro` with `cleanup_generated_actors` in dry-run mode and real mode to remove the temporary level actors.
- Cover structured negative paths for an unsupported editor macro and unsupported level pattern.

## Latest Passing Run

- Timestamp: `20260423_222221`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MacroUtilityRound1\20260423_222221`
- Generated Blueprint: `/Game/UEBridgeMCPValidation/MacroUtilityRound1/BP_GeneratedPattern_20260423_222221`
- Temporary level actor prefix: `UEBMCP_MacroUtility_20260423_222221`
- Runtime tool count: `145`

## Notes

- `run-editor-macro` is intentionally curated and does not accept arbitrary script text.
- `generate-blueprint-pattern` delegates to the already-smoked `create-blueprint-pattern` implementation instead of creating a second Blueprint pattern backend.
- `generate-level-pattern` uses engine-only StaticMeshActor cube scaffolds and does not depend on project content.
