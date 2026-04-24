# Niagara Round 1 Smoke Checklist

Scope:

- Covers Niagara Phase 2A only.
- Does not validate Audio, MetaSound, arbitrary Niagara graph editing, or full module stack authoring.
- Host project is `G:\UEProjects\MyProject`.
- Validation assets live under `/Game/UEBridgeMCPValidation/NiagaraRound1`.
- Evidence lives under `G:\UEProjects\UEBridgeMCP\Tmp\Validation\NiagaraRound1\<timestamp>`.

Checklist:

- `tools/list` shows the five conditional Niagara Phase 2A tools when Niagara is available.
- `get-project-info.optional_capabilities.niagara_available` is `true` in the Niagara-enabled host.
- `create-niagara-system-from-template` creates a new Niagara system with a timestamped name and can compile/save it.
- `query-niagara-system-summary` returns system readiness, emitter count, user parameter count, and user parameter details.
- `edit-niagara-user-parameters` covers `dry_run=true`, `add_parameter`, `set_default`, `rename_parameter`, `remove_parameter`, final compile, save, and at least one structured failure.
- `query-niagara-emitter-summary` is covered through a no-emitter structured failure for an empty/default system, or a positive emitter-handle summary if a template with emitters is available.
- `apply-niagara-system-to-actor` creates or updates a NiagaraComponent on an editor-world actor, applies at least one override, and saves or reports the modified world.
- In editor-world smoke, `activate_now=true` should be reported as deferred with a warning instead of starting Niagara simulation on the editor GameThread.
- Regression check: existing Blueprint Round 1/2 tools remain visible after Niagara conditional registration is enabled.

Evidence expectations:

- Save raw MCP responses for tool visibility, project info, create, query, parameter edit, actor apply, and negative paths.
- Save a compact `summary.json` containing created asset paths, actor/component names, success flags, warning counts, and any structured error codes.
