# Macro / Utility Round 1 Provenance

Date: 2026-04-23

Phase: Macro / Utility Phase 4C v1

Host project: `G:\UEProjects\MyProject`

Implementation repository: `G:\UEProjects\UEBridgeMCP`

## Implementation

- Added five always-on macro and utility tools in the core editor module.
- `query-workspace-health` reports project/plugin/server/world/path/capability/dirty-package state.
- `run-project-maintenance-checks` composes workspace health, conservative unused-asset checks, and optional Blueprint compile checks.
- `generate-blueprint-pattern` wraps `create-blueprint-pattern` and adds a dry-run delegated plan path.
- `generate-level-pattern` creates curated engine-only editor-world scaffold patterns.
- `run-editor-macro` exposes a small curated macro catalog and deliberately does not expose arbitrary scripting.

## Verification

- Build: `MyProjectEditor Win64 Development` succeeded with `-MaxParallelActions=4`.
- Runtime tool count at `tools/list`: `145`.
- Final evidence directory: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MacroUtilityRound1\20260423_222221`
- Runtime-created validation Blueprint:
- `/Game/UEBridgeMCPValidation/MacroUtilityRound1/BP_GeneratedPattern_20260423_222221`
- Temporary editor-world actor prefix:
- `UEBMCP_MacroUtility_20260423_222221`

## Regressions Covered

- `tools/list` visibility for all five Phase 4C tools.
- Workspace health returns project, server, world, optional capability, and dirty package state.
- Maintenance checks return structured check bundles in dry-run mode and with a real Blueprint compile check.
- Blueprint pattern generation dry-run and real create/compile/save paths both pass.
- Level pattern generation dry-run and real editor-world spawn paths both pass.
- Editor macro health, compile dry-run, compile real, cleanup dry-run, and cleanup real paths pass.
- Unsupported editor macro and unsupported level pattern return structured `UEBMCP_INVALID_ACTION`.

## Boundaries

- This round does not create a universal macro/script executor.
- This round does not add arbitrary C++/config/source full-text indexing.
- This round does not replace `run-workflow-preset`; it adds curated utility entrypoints for common repeated workflows.
- This round does not introduce project-content dependencies for generated level patterns.
