# Gameplay Runtime Round 1 Smoke Checklist

Scope: Gameplay Runtime Phase 3C v1. This round covers read-only runtime actor state, live AbilitySystemComponent state, and read-only gameplay collision traces in editor and PIE worlds.

Host project: `G:\UEProjects\MyProject`

Validation root: `/Game/UEBridgeMCPValidation/GameplayRuntimeRound1`

Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\GameplayRuntimeRound1\<timestamp>`

## Checklist

- Confirm `tools/list` exposes `query-runtime-actor-state`, `query-ability-system-state`, and `trace-gameplay-collision`.
- Create a temporary Actor Blueprint under the validation root.
- Add an `AbilitySystemComponent` to that Actor Blueprint with `manage-ability-system-bindings`, then compile/save it.
- Runtime-seed a temporary StaticMeshActor with blocking collision and a temporary instance of the ASC Actor Blueprint.
- Start PIE through `pie-session` and poll `get-state` until `state=running`.
- Run `query-runtime-actor-state` against the PIE StaticMeshActor and verify the response world type is `PIE`, component count is non-zero, and collision details are present.
- Run `query-ability-system-state` against the PIE ASC actor and verify the response world type is `PIE`, the ability system payload is valid, and the component name is returned.
- Run `trace-gameplay-collision` in the PIE world and verify a blocking hit is returned with structured hit actor/component data.
- Run `query-ability-system-state` against the non-GAS StaticMeshActor and verify structured `UEBMCP_ABILITY_SYSTEM_NOT_FOUND`.
- Stop PIE, poll until `state=not_running`, and clean up temporary editor-world actors.

## Latest Passing Run

- Timestamp: `20260423_211350`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\GameplayRuntimeRound1\20260423_211350`
- Asset: `/Game/UEBridgeMCPValidation/GameplayRuntimeRound1/BP_GameplayRuntimePIE_20260423_211350`
- Runtime labels:
- `UEBridgeMCP_RuntimePIE_20260423_211350`
- `UEBridgeMCP_RuntimePIEGAS_20260423_211350`

## Notes

- The final acceptance run uses PIE world assertions; an earlier editor-world-only exploratory run is retained under the same evidence root for debugging context.
- `query-ability-system-state` is intentionally read-only and does not grant abilities or mutate runtime tags.
- `trace-gameplay-collision` is read-only and does not advance simulation.
