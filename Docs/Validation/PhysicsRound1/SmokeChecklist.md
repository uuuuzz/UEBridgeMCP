# Physics Round 1 Smoke Checklist

Scope:

- Covers Physics Phase 3A only.
- Does not validate GAS, Chaos runtime simulation assertions, destructive physics-world changes, or PIE gameplay behavior.
- Host project is `G:\UEProjects\MyProject`.
- Validation assets live under `/Game/UEBridgeMCPValidation/PhysicsRound1`.
- Evidence lives under `G:\UEProjects\UEBridgeMCP\Tmp\Validation\PhysicsRound1\<timestamp>`.

Checklist:

- `tools/list` shows all six Physics Phase 3A tools: `query-physics-summary`, `edit-collision-settings`, `edit-physics-simulation`, `create-physics-constraint`, `edit-physics-constraint`, and `apply-physical-material`.
- `query-physics-summary` returns a valid world summary.
- Runtime-created editor-world actors can be queried with actor scope, including PrimitiveComponent collision and simulation details.
- `edit-collision-settings` covers `dry_run=true`, positive collision profile / enabled mode / object channel / response edits, rollback, and save paths.
- `edit-physics-simulation` covers `dry_run=true`, positive simulate-physics, gravity, mass, mass scale, damping, wake/sleep, rollback, and save paths.
- `apply-physical-material` covers `dry_run=true` and positive PhysicalMaterial override application using a runtime-created PhysicalMaterial asset.
- `create-physics-constraint` covers `dry_run=true` and positive constraint creation between two actor PrimitiveComponents.
- `edit-physics-constraint` covers positive constraint edits, including disable-collision, projection, linear limits, angular limits, and break thresholds.
- At least one missing component path returns a structured error.
- Actor-specific `query-physics-summary` for a missing actor returns a structured `UEBMCP_ACTOR_NOT_FOUND` error instead of silently falling back to world scope.
- Temporary editor-world actors created during smoke are cleaned up after final assertions.

Evidence expectations:

- Save raw MCP responses for tool visibility, project info, seed fixture creation, world query, actor query, dry-run/apply paths, constraint create/edit paths, negative paths, final actor summary, and cleanup.
- Save a compact `summary.json` containing target actor names, PhysicalMaterial path, requested and actual constraint component names, success flags, final primitive/constraint counts, structured error codes, and cleanup status.
- When a requested constraint component name ends in numeric-looking segments, use the actual component name returned by `create-physics-constraint` for follow-up edits because Unreal's `FName` compaction may shorten the visible component name.
