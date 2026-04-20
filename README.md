# UEBridgeMCP

**Native C++ Model Context Protocol (MCP) plugin for Unreal Engine 5.6+**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.6%2B-313131?logo=unrealengine)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/platform-Win64%20%7C%20Mac%20%7C%20Linux-lightgrey.svg)](#)
[![Release](https://img.shields.io/github/v/release/uuuuzz/UEBridgeMCP?include_prereleases&sort=semver)](https://github.com/uuuuzz/UEBridgeMCP/releases)
[![GitHub stars](https://img.shields.io/github/stars/uuuuzz/UEBridgeMCP?style=social)](https://github.com/uuuuzz/UEBridgeMCP/stargazers)
[![GitHub issues](https://img.shields.io/github/issues/uuuuzz/UEBridgeMCP)](https://github.com/uuuuzz/UEBridgeMCP/issues)

**Language:** **English** | [简体中文](README.zh-CN.md)

## Overview

UEBridgeMCP is a native C++ Unreal Engine plugin that exposes the Unreal Editor to any MCP-compatible AI client over Streamable HTTP. It embeds the MCP server directly inside the editor process, so tools can inspect and edit Blueprints, levels, assets, materials, widgets, StateTrees, PIE sessions, and build workflows without a separate bridge process.

**Acknowledgments:** This project is a heavily extended and optimized fork of [yes-ue-mcp](https://github.com/softdaddy-o/yes-ue-mcp) by softdaddy-o. Special thanks to the original author for the foundational MCP HTTP server architecture.

**Current release:** `v1.19.0`

**Highlights:**
- **Native UE integration** - uses Unreal's built-in HTTP stack and editor APIs
- **46 built-in tools** - the authoritative list lives in `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp`
- **Cross-client compatible** - works with Claude Code, Claude Desktop, Cursor, Continue, Windsurf, and other MCP clients
- **Project-agnostic** - no game-specific coupling; can be moved between UE 5.6+ C++ projects
- **Extensible** - add custom tools by subclassing `UMcpToolBase`

## Documentation Map

| Document | Description |
| --- | --- |
| [Tools Reference](Docs/Tools-Reference.md) | Full list of the built-in tools, grouped by workflow and subsystem |
| [Tool Development](Docs/ToolDevelopment.md) | How to implement, register, validate, and maintain your own MCP tools |
| [Troubleshooting](Docs/Troubleshooting.md) | Known failure modes, connection issues, PIE caveats, and debugging steps |
| [Architecture](Docs/Architecture.md) | Module layout, request lifecycle, registry warmup, and threading model |
| [Chinese Documentation Index](README.zh-CN.md) | Chinese landing page with links to the translated docs |

## Quick Start

### Requirements

- Unreal Engine **5.6+**
- A **C++ Unreal project** (Blueprint-only projects should be converted by adding any C++ class)
- Core plugin dependencies enabled in `UEBridgeMCP.uplugin`:
  - `EditorScriptingUtilities`
  - `GameplayAbilities`
  - `StateTree`
  - `GameplayStateTree`
- `PythonScriptPlugin` powers `run-python-script`. The plugin descriptor marks it optional, but the current source build links against it directly in `Source/UEBridgeMCPEditor/UEBridgeMCPEditor.Build.cs`, so keep it enabled unless you intentionally remove Python tooling.

### Installation

1. Copy the plugin into your project:

```text
<YourProject>/Plugins/UEBridgeMCP/
```

2. Enable it in your `.uproject`:

```json
{
  "Plugins": [
    { "Name": "UEBridgeMCP", "Enabled": true }
  ]
}
```

3. Regenerate project files and build your editor target.
4. Launch the editor and confirm the module loads successfully.

### Configuration

Server settings live in `Config/DefaultUEBridgeMCP.ini`:

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
ServerPort=8080
bAutoStartServer=true
BindAddress=127.0.0.1
LogLevel=Log
```

**Important:** on Windows, prefer `127.0.0.1` instead of `localhost`. Some clients resolve `localhost` to IPv6 (`[::1]`) while the server commonly binds IPv4, which causes avoidable connection failures.

### Client Configuration

Any MCP client that supports HTTP transport can connect. Examples:

**Claude Code CLI**

```bash
claude mcp add --transport http unreal-engine http://127.0.0.1:8080/mcp
```

**Claude Desktop** (`claude_desktop_config.json`)

```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**Cursor** (`.cursor/mcp.json`)

```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**VS Code Continue** (`.continue/config.json`)

```json
{
  "mcpServers": [
    {
      "name": "unreal-engine",
      "transport": {
        "type": "http",
        "url": "http://127.0.0.1:8080/mcp"
      }
    }
  ]
}
```

### Sanity Check

Use a `tools/list` request to confirm the server is reachable:

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

If the plugin is running correctly, the response should contain the registered tool set for the current editor session.

## Built-in Tool Surface

The authoritative registration site is:

```text
Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp
```

At `v1.19.0`, the plugin registers **46 built-in tools** across the following groups:

| Group | Count | Examples |
| --- | ---: | --- |
| Query and inspection | 15 | `query-blueprint-summary`, `query-asset`, `get-asset-diff`, `find-references`, `get-logs` |
| Creation and editing | 14 | `create-asset`, `add-widget`, `edit-blueprint-graph`, `edit-level-batch`, `apply-material` |
| StateTree | 5 | `query-statetree`, `add-statetree-state`, `add-statetree-transition` |
| PIE, scripting, build, and RPC | 8 | `run-python-script`, `trigger-live-coding`, `pie-session`, `pie-input`, `wait-for-world-condition`, `call-function` |
| High-level orchestration | 4 | `blueprint-scaffold-from-spec`, `query-gameplay-state`, `auto-fix-blueprint-compile-errors`, `generate-level-structure` |

See [Tools Reference](Docs/Tools-Reference.md) for the full list and per-tool summaries.

## Architecture Snapshot

```text
MCP Client
  -> HTTP POST /mcp
  -> UEBridgeMCPEditor module
  -> FMcpServer
  -> FMcpToolRegistry
  -> UMcpToolBase-derived tools
  -> Unreal Editor subsystems / assets / worlds / PIE
```

The plugin ships two modules:

- `UEBridgeMCP` - runtime protocol types, schema helpers, base classes, and tool registry
- `UEBridgeMCPEditor` - editor-only server, subsystem integration, built-in tools, and toolbar/status integration

See [Architecture](Docs/Architecture.md) for the full lifecycle, warmup behavior, and threading constraints.

## Extending UEBridgeMCP

To add a new tool:

1. Create a new `UMcpToolBase` subclass under `Source/UEBridgeMCPEditor/Public/Tools/` and `Private/Tools/`
2. Override `GetToolName`, `GetToolDescription`, `GetInputSchema`, `GetRequiredParams`, and `Execute`
3. Register the class in `FUEBridgeMCPEditorModule::RegisterBuiltInTools()`
4. Rebuild or trigger Live Coding
5. Add automated validation if your branch or repo contains a test harness; otherwise document a focused live-editor validation recipe such as a `tools/list` or `tools/call` request

See [Tool Development](Docs/ToolDevelopment.md) for a fuller guide and recommended implementation rules.

## Versioning

UEBridgeMCP follows semantic versioning for public releases.

- Keep `VersionName` in `UEBridgeMCP.uplugin` and `UEBRIDGEMCP_VERSION` in `Source/UEBridgeMCP/Public/UEBridgeMCP.h` identical.
- Bump both in the same PR when public tool names, schemas, defaults, or externally visible behavior changes.
- Update `CHANGELOG.md` and `RELEASE_NOTES.md` alongside the version bump.

## Troubleshooting Entry Points

Start with [Troubleshooting](Docs/Troubleshooting.md) if you hit any of these problems:

- The client cannot connect to `http://127.0.0.1:8080/mcp`
- `tools/list` is empty or missing expected tools
- PIE start/stop appears stuck
- Python execution fails or crashes the editor
- Live Coding or rebuild operations do not reflect recent code changes
- Multiple UE projects are fighting over the same server port

## License

GNU General Public License v3.0 - see [LICENSE](LICENSE) for details.

## Links

- [GitHub Repository](https://github.com/uuuuzz/UEBridgeMCP)
- [Changelog](CHANGELOG.md)
- [Release Notes](RELEASE_NOTES.md)
- [MCP Overview](https://modelcontextprotocol.io)
- [MCP Specification](https://modelcontextprotocol.io/specification)
- [Unreal Engine Documentation](https://docs.unrealengine.com/5.6/)

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=uuuuzz/UEBridgeMCP&type=Date)](https://star-history.com/#uuuuzz/UEBridgeMCP&Date)
