# Engine API Round 1 Provenance

Date: 2026-04-23

Phase: Engine API Phase 4B v1

Host project: `G:\UEProjects\MyProject`

Implementation repository: `G:\UEProjects\UEBridgeMCP`

## Implementation

- Added four always-on local Engine API helper tools:
- `query-engine-api-symbol`
- `query-class-member-summary`
- `query-plugin-capabilities`
- `query-editor-subsystem-summary`
- Added shared helper code in `Source/UEBridgeMCPEditor/Private/Tools/Analysis/EngineApiToolUtils.*` for class resolution, class/function/property/plugin serialization, flag summaries, metadata extraction, and ranked result trimming.
- Registered the tools from `RegisterBuiltInTools()` next to existing class hierarchy and search/reference tools.
- Reused local Unreal reflection, `IPluginManager`, `FModuleManager`, and subsystem APIs; no external documentation lookup is involved.

## Verification

- Build: `MyProjectEditor Win64 Development` succeeded with `-MaxParallelActions=4`.
- Runtime tool count at `tools/list`: `140`.
- Final evidence directory: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\EngineApiRound1\20260423_220509`

## Regressions Covered

- `tools/list` visibility for all four Engine API Phase 4B tools.
- `query-engine-api-symbol` returns ranked class and function results.
- `query-class-member-summary` resolves `Actor` and returns property/function sections.
- `query-plugin-capabilities` resolves `UEBridgeMCP`, includes module descriptors, and supports ranked plugin queries.
- `query-editor-subsystem-summary` returns editor/engine subsystem summaries and active-state fields.
- Missing class lookup returns structured `UEBMCP_CLASS_NOT_FOUND`.

## Boundaries

- This round does not proxy web documentation or require network access.
- This round does not create a persistent source-code or config text index.
- This round does not mutate plugin enablement, load new modules, or initialize inactive subsystems.
