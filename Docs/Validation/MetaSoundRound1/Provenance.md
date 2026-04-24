# MetaSound Round 1 Provenance

Purpose:

- Record the first MetaSound Phase 2C implementation and smoke evidence.
- Keep the boundary explicit: this round is not runtime playback validation, arbitrary DSP graph generation, or deeper MetaSound asset migration tooling.

Implementation provenance:

- MetaSound tools are core-editor conditional tools registered from `RegisterBuiltInTools()` only when the engine MetaSound modules are available.
- The editor module now depends on `MetasoundEngine`, `MetasoundFrontend`, and `MetasoundEditor`.
- Shared MetaSound behavior is centralized in `Source/UEBridgeMCPEditor/Private/Tools/MetaSound/MetaSoundToolUtils.*`.
- Tool entrypoints are `query-metasound-summary`, `create-metasound-source`, `edit-metasound-graph`, and `set-metasound-input-defaults`.
- V1 uses official engine builder APIs for Source creation and existing Source graph edits, and intentionally limits graph editing to explicit, structural operations.

Validation provenance:

- Host: `G:\UEProjects\MyProject`.
- Asset root: `/Game/UEBridgeMCPValidation/MetaSoundRound1`.
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MetaSoundRound1`.
- Final smoke evidence for this implementation is `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MetaSoundRound1\20260423_192531`.
- Runtime-created MetaSound Source assets use timestamped names such as `MS_MetaSoundRound1_<timestamp>`.
- The final smoke created `/Game/UEBridgeMCPValidation/MetaSoundRound1/MS_MetaSoundRound1_20260423_192531`.
- The same evidence directory also includes a post-patch optional-field check using `/Game/UEBridgeMCPValidation/MetaSoundRound1/MS_MetaSoundRound1_Optional_20260423_192531`.

Known v1 boundaries:

- Supported v1 literal defaults are bool, int32, float, and string; trigger/audio graph inputs are accepted structurally but do not have semantic default values.
- `edit-metasound-graph` exposes class-name node insertion and explicit connection operations, but the smoke keeps to graph I/O and layout to avoid relying on unstable engine class-name fixtures.
- Runtime audio playback and actor component application continue to live in the Audio tool family.
- Arbitrary MetaSound graph synthesis, template catalogs, preset migration, and DSP-level validation remain future work.
