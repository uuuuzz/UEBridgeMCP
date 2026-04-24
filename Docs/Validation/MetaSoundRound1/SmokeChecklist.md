# MetaSound Round 1 Smoke Checklist

Scope:

- Covers MetaSound Phase 2C only.
- Does not validate Audio, Niagara, runtime playback, arbitrary MetaSound graph synthesis, or full DSP graph authoring.
- Host project is `G:\UEProjects\MyProject`.
- Validation assets live under `/Game/UEBridgeMCPValidation/MetaSoundRound1`.
- Evidence lives under `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MetaSoundRound1\<timestamp>`.

Checklist:

- `tools/list` shows the four conditional MetaSound Phase 2C tools when MetaSound modules are available: `query-metasound-summary`, `create-metasound-source`, `edit-metasound-graph`, and `set-metasound-input-defaults`.
- `get-project-info.optional_capabilities.metasound_available` is `true` in the MetaSound-enabled host.
- `create-metasound-source` creates a timestamped MetaSound Source under `/Game/UEBridgeMCPValidation/MetaSoundRound1`, sets output format, seeds supported v1 graph inputs, and saves the asset.
- `query-metasound-summary` returns the created MetaSound Source summary, including input/output counts, interface count, node count, and optional graph nodes/edges.
- `set-metasound-input-defaults` covers `dry_run=true`, positive bool/float default edits, rollback, and save paths.
- `edit-metasound-graph` covers `dry_run=true`, positive graph input creation, `layout_graph`, rollback, and save paths.
- At least one negative path returns a structured error, such as setting a default for a missing graph input.
- The created MetaSound Source remains queryable after all edits and reports the expected increased input count.

Evidence expectations:

- Save raw MCP responses for tool visibility, project info, source creation, initial/final summaries, default dry-run/apply, graph edit dry-run/apply, and negative paths.
- Save a compact `summary.json` containing the created asset path, tool visibility, optional capability flag, success flags, final graph counts, and structured error code.
