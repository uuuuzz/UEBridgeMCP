# Changelog

All notable changes to **UEBridgeMCP** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Changes currently on `main` after the `v1.19.0` public release.

### Added
- Expanded the dynamic MCP tool surface across Blueprint authoring, asset and
  data management, search, gameplay systems, GAS, input, AI, navigation,
  networking, animation, Sequencer, widgets, materials, static meshes, audio,
  physics, performance, editor interaction, world production, Landscape,
  Foliage, World Partition, Niagara, MetaSound, and workflow orchestration.
- Added optional extension modules for Control Rig, PCG, and External AI tools.
- Added built-in MCP resources, prompts, and workflow preset infrastructure for
  repeatable agent workflows such as animation smoke checks, performance
  triage, Sequencer editing, and world production.
- Added release and capability validation assets, including smoke checklists,
  provenance templates, Step 6 validation artifacts, release preflight checks,
  and compatibility alias verification.
- Added detailed English and Simplified Chinese documentation for architecture,
  tool development, tool reference, capability coverage, troubleshooting, and
  release preflight.
- Added project community and distribution polish, including QQ group assets and
  plugin filtering configuration.

### Changed
- Migrated the plugin codebase and validation target toward Lyra on Unreal
  Engine 5.6.
- Made the runtime inventory explicitly dynamic: `tools/list` and
  `initialize.capabilities.tools.registeredCount` are the authoritative source
  for available tools.
- Expanded compatibility naming support with alias validation for clients and
  workflows that use alternate tool names.
- Refined README content and documentation navigation for the expanded tool
  surface.

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

[Unreleased]: https://github.com/uuuuzz/UEBridgeMCP/compare/v1.19.0...HEAD
[1.19.0]: https://github.com/uuuuzz/UEBridgeMCP/releases/tag/v1.19.0
