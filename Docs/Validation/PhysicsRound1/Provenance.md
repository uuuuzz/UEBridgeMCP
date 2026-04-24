# Physics Round 1 Provenance

Purpose:

- Record the first Physics Phase 3A implementation and smoke evidence.
- Keep the boundary explicit: this round is not GAS, PIE runtime physics validation, Chaos deep diagnostics, or destructive physics-world editing.

Implementation provenance:

- Physics tools are core-editor always-on tools registered from `RegisterBuiltInTools()`.
- The editor module now depends on `PhysicsCore` so PhysicalMaterial and physics component APIs link reliably.
- Shared Physics behavior is centralized in `Source/UEBridgeMCPEditor/Private/Tools/Physics/PhysicsToolUtils.*`.
- Tool entrypoints are `query-physics-summary`, `edit-collision-settings`, `edit-physics-simulation`, `create-physics-constraint`, `edit-physics-constraint`, and `apply-physical-material`.
- V1 focuses on safe editor-world actor/component edits with dry-run, rollback, save, and structured negative paths.

Validation provenance:

- Host: `G:\UEProjects\MyProject`.
- Asset root: `/Game/UEBridgeMCPValidation/PhysicsRound1`.
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\PhysicsRound1`.
- Final smoke evidence for this implementation is `G:\UEProjects\UEBridgeMCP\Tmp\Validation\PhysicsRound1\20260423_195106`.
- Runtime-created editor-world actors used timestamped labels `PhysicsRound1_A_20260423_195106` and `PhysicsRound1_B_20260423_195106`.
- The final smoke created `/Game/UEBridgeMCPValidation/PhysicsRound1/PM_PhysicsRound1_20260423_195106` as the PhysicalMaterial fixture.
- The final smoke requested `PC_PhysicsRound1_20260423_195106` and then used the actual returned component name `PC_PhysicsRound1_20260423_0` for follow-up constraint edits because Unreal compacted the trailing numeric `FName`.
- Temporary editor-world actors were cleaned up after final assertions.

Known v1 boundaries:

- Physics Phase 3A edits existing editor-world actor components; it does not run a PIE physics simulation or assert runtime motion.
- Constraint v1 covers common linear/angular motion, limits, projection, disable-collision, and break threshold settings; advanced drive/profile authoring remains future work.
- `apply-physical-material` sets component overrides only; bulk asset reassignment and material-slot level policy tooling remain future work.
- GAS and deeper gameplay runtime validation are planned separately and are not part of this Physics smoke.
