# Engine API Round 1 Smoke Checklist

Scope: Engine API Phase 4B v1. This round covers local reflection search, class member summaries, plugin capability summaries, and editor subsystem summaries.

Host project: `G:\UEProjects\MyProject`

Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\EngineApiRound1\<timestamp>`

## Checklist

- Confirm `tools/list` exposes `query-engine-api-symbol`, `query-class-member-summary`, `query-plugin-capabilities`, and `query-editor-subsystem-summary`.
- Run `query-engine-api-symbol` for a known class query such as `Actor` and verify `/Script/Engine.Actor` or `Actor` is returned.
- Run `query-engine-api-symbol` for a known function query such as `LineTrace` and verify function symbols are returned.
- Run `query-class-member-summary` for `Actor` with a member query such as `Location` and verify both function/property sections are structured.
- Run `query-plugin-capabilities` for `UEBridgeMCP` and verify the plugin plus module descriptors are returned.
- Run `query-plugin-capabilities` with a fuzzy query such as `Niagara` and verify ranked plugin results.
- Run `query-editor-subsystem-summary` with editor/engine families and verify subsystem class results plus active-state fields.
- Cover a structured negative path by querying a missing class name through `query-class-member-summary`.

## Latest Passing Run

- Timestamp: `20260423_220509`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\EngineApiRound1\20260423_220509`
- Runtime tool count: `140`

## Notes

- Phase 4B is local-only. It uses loaded Unreal reflection data, plugin descriptors, module state, and subsystem class/instance availability.
- It does not fetch external documentation, mirror online docs, or index arbitrary source/config files.
- Reflection search is capped by `max_classes` and `limit` to avoid long editor GameThread stalls.
