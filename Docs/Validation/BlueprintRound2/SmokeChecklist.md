# Blueprint Round 2 Smoke Checklist

## Goal

Validate Blueprint Phase 1C in `G:\UEProjects\MyProject`: compile analysis, structural fixups, interface conformance, and curated Actor patterns.

## Host And Asset Scope

- Host project: `G:\UEProjects\MyProject`
- MCP endpoint: `http://127.0.0.1:8080/mcp`
- Validation asset root: `/Game/UEBridgeMCPValidation/BlueprintRound2`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\<timestamp>`
- No dedicated automated harness is required for this round.

## Protocol And Visibility

- [x] `initialize` succeeds.
- [x] `tools/list` succeeds.
- [x] `tools/list` contains `analyze-blueprint-compile-results`.
- [x] `tools/list` contains `apply-blueprint-fixups`.
- [x] `tools/list` contains `create-blueprint-pattern`.
- [x] `tools/list` still contains `compile-assets`, `auto-fix-blueprint-compile-errors`, and `blueprint-scaffold-from-spec`.

## Analyze Validation

- [x] A clean Actor Blueprint returns `compile.success=true`.
- [x] A clean Actor Blueprint returns `issues=[]`.
- [x] A seeded broken Blueprint returns at least one compile diagnostic.
- [x] A seeded broken Blueprint returns structural findings.
- [x] Suggested fixups match the v1 mapping for unresolved or broken references.

## Fixup Validation

- [x] `dry_run=true` succeeds without modifying assets.
- [x] `refresh_all_nodes` followed by `remove_orphan_pins` removes orphan pins.
- [x] `conform_implemented_interfaces` repairs a missing interface graph.
- [x] A structured invalid-action failure is returned.
- [x] `rollback_on_error=true` leaves no successful partial action for the invalid-action path.

## Interface Conformance Fixture

- [x] Runtime-create a Blueprint Interface with `create-asset(asset_class="BlueprintInterface")`.
- [x] Runtime-create an Actor Blueprint target.
- [x] Add the empty interface with `sync_graphs=false`.
- [x] Add a non-void interface function after the target already implements the interface.
- [x] Confirm `query-blueprint-findings` reports `missing_interface_graph`.
- [x] Apply `conform_implemented_interfaces`.
- [x] Confirm findings disappear and the target Blueprint compiles.

## Pattern Validation

- [x] `create-blueprint-pattern(pattern="logic_actor_skeleton")` creates and compiles an Actor Blueprint.
- [x] `create-blueprint-pattern(pattern="toggle_state_actor")` creates and compiles an Actor Blueprint.
- [x] `create-blueprint-pattern(pattern="interaction_stub_actor")` creates and compiles an Actor Blueprint.
- [x] Pattern graphs are laid out and include comment regions.
- [x] Reusing an existing pattern asset path returns `UEBMCP_ASSET_ALREADY_EXISTS`.

## Regression Validation

- [x] `compile-assets` still compiles the Phase 1C pattern assets and returns diagnostics without schema changes.
- [x] `auto-fix-blueprint-compile-errors` still accepts the original four strategies in `dry_run`.
- [x] `blueprint-scaffold-from-spec` remains registered but is not used as the curated pattern backend.

## Accepted Evidence

- Latest accepted evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254`
- Summary file: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254\summary.json`
