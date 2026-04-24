# Niagara Round 1 Provenance

Purpose:

- Record the first Niagara Phase 2A implementation and smoke evidence.
- Keep the boundary explicit: this round is not Audio, MetaSound, or arbitrary Niagara graph editing.

Implementation provenance:

- Niagara tools are core-editor conditional tools, registered only when `FMcpOptionalCapabilityUtils::IsNiagaraAvailable()` is true.
- The core module now depends on `Niagara` and `NiagaraEditor`, and the plugin descriptor declares Niagara as an optional plugin dependency.
- Shared Niagara behavior is centralized in `Source/UEBridgeMCPEditor/Private/Tools/Niagara/NiagaraToolUtils.*`.
- Tool entrypoints are `query-niagara-system-summary`, `query-niagara-emitter-summary`, `create-niagara-system-from-template`, `edit-niagara-user-parameters`, and `apply-niagara-system-to-actor`.

Validation provenance:

- Host: `G:\UEProjects\MyProject`.
- Asset root: `/Game/UEBridgeMCPValidation/NiagaraRound1`.
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\NiagaraRound1`.
- Runtime-created assets should use timestamped names such as `NS_NiagaraRound1_<timestamp>` and actor labels such as `NiagaraRound1Actor_<timestamp>`.

Known v1 boundaries:

- User parameter editing supports bool, int32, float, vector2, vector3, position, vector4, and color values.
- Emitter summaries are shallow and do not edit emitter stacks.
- `create-niagara-system-from-template` creates an empty/default system if no template is supplied; a richer template catalog remains future work.
- `apply-niagara-system-to-actor` targets editor-world actor components and does not spawn transient runtime effects.
- Immediate component activation is deferred in editor worlds to avoid blocking the editor GameThread; PIE/runtime activation can be validated separately in a later pass.
