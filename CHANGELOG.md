# Changelog

All notable changes to **UEBridgeMCP** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.19.0] - 2026-04-19

First public release on GitHub.

### Added
- Native C++ Model Context Protocol (MCP) server for Unreal Engine **5.6+**,
  hosted directly inside the UE Editor via the built-in `FHttpServerModule`
  (no external process or Python bridge required).
- **v2 agent-first tool surface** organised around the workflow
  `summary query -> targeted detail -> transactional edit -> compile/save -> assert`:
  - Query / detail: `query-blueprint-summary`, `query-blueprint-graph-summary`,
    `query-blueprint-node`, `query-world-summary`, `query-level-summary`,
    `query-actor-detail`, `query-material-summary`, `query-material-instance`.
  - Batch / assert: `edit-blueprint-graph`, `edit-blueprint-members`,
    `edit-blueprint-components`, `edit-level-batch`,
    `edit-material-instance-batch`, `assert-world-state`.
- **Structured response contract** for every v2 tool: `content`,
  `structuredContent`, optional `isError`, plus `_meta.diagnostics`,
  `_meta.timing`, `_meta.stats`.
- **Stable actor handles** (`entity_id` + `resource_path` + `display_name` +
  `session_id`) so agents can reference actors across calls without relying on
  fragile display names.
- **Python scripting** via `run-python-script` (inline code or script file,
  with arguments accessible through `unreal.get_mcp_args()`).
- **Blueprint analysis suite**: `analyze-blueprint`,
  `get-blueprint-functions`, `get-blueprint-variables`,
  `get-blueprint-components`, `get-blueprint-graph`, `get-blueprint-node`,
  `get-blueprint-defaults`.
- **Level / World tooling**: `query-level`, `get-actor-details`.
- **Project configuration**: `get-project-settings` (input / collision / tags /
  maps).
- **Engine analysis**: `get-class-hierarchy`, `inspect-data-asset`.
- **Asset management**: `search-assets`.
- **Cross-client support** out of the box: Claude Code CLI, Claude Desktop,
  Cursor, Windsurf, VS Code Continue, and any other Streamable-HTTP MCP client.
- **Extensibility layer** via `UMcpToolBase` + `FMcpToolRegistry` so projects
  can register their own custom MCP tools without forking the plugin.
- Default configuration at `Config/DefaultUEBridgeMCP.ini`
  (`ServerPort=8080`, `bAutoStartServer=true`, `BindAddress=127.0.0.1`).

### Known Limitations
- Marked as **experimental** in `.uplugin` (`IsExperimentalVersion = true`);
  public API may still evolve before 2.0.
- Server binds to `127.0.0.1` only by default; LAN / remote access requires
  opting in by changing `BindAddress` and understanding the security
  implications.
- PIE input injection (`pie-input` with raw keys / axes) is known to be
  unreliable for legacy `AxisMapping` pipelines; prefer high-level actions
  (`action:move-to`) or direct `AddMovementInput` calls for end-to-end
  character movement verification.

[1.19.0]: https://github.com/uuuuzz/UEBridgeMCP/releases/tag/v1.19.0
