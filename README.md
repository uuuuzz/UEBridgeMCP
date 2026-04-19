# UEBridgeMCP

**Native C++ Model Context Protocol (MCP) plugin for Unreal Engine 5.6+**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.6%2B-313131?logo=unrealengine)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/platform-Win64%20%7C%20Mac%20%7C%20Linux-lightgrey.svg)](#)
[![Release](https://img.shields.io/github/v/release/uuuuzz/UEBridgeMCP?include_prereleases&sort=semver)](https://github.com/uuuuzz/UEBridgeMCP/releases)
[![GitHub stars](https://img.shields.io/github/stars/uuuuzz/UEBridgeMCP?style=social)](https://github.com/uuuuzz/UEBridgeMCP/stargazers)
[![GitHub issues](https://img.shields.io/github/issues/uuuuzz/UEBridgeMCP)](https://github.com/uuuuzz/UEBridgeMCP/issues)

📖 **Language / 语言**：**English** | [简体中文](README.zh-CN.md)

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
  - [Installation](#installation)
  - [Configuration](#configuration)
  - [Client Configuration](#client-configuration)
- [Available Tools](#available-tools)
  - [v2 core workflow](#v2-core-workflow)
  - [v2 query / detail tools](#v2-querydetail-tools)
  - [v2 batch / assert tools](#v2-batchassert-tools)
  - [Structured response contract](#structured-response-contract)
  - [Python scripting](#python-scripting-new-in-v1100)
  - [Blueprint analysis](#blueprint-analysis-7-tools)
  - [Level / World tools](#levelworld-tools-2-tools)
  - [Project configuration](#project-configuration-1-tool)
  - [Analysis tools](#analysis-tools-2-tools)
  - [Asset management](#asset-management-1-tool)
- [Architecture](#architecture)
- [Development](#development)
- [License](#license)
- [Contributing](#contributing)
- [Links](#links)

## Overview

ue-bridge-mcp is a native C++ Unreal Engine plugin that implements the [Model Context Protocol (MCP)](https://modelcontextprotocol.io) over Streamable HTTP. It enables AI assistants like Claude, Cursor, Windsurf, and others to interact directly with the Unreal Editor.

**Key Features:**
- ✅ **Zero Dependencies** - Uses UE's built-in `FHttpServerModule`
- ✅ **Cross-LLM Compatible** - Works with Claude, Cursor, Windsurf, VS Code Copilot, Continue, OpenAI, and more
- ✅ **Editor-Integrated** - HTTP server runs directly in UE Editor, no separate process needed
- ✅ **Extensible** - Easy-to-use tool registration system for custom MCP tools

## Quick Start

### Installation

1. Clone into your project's `Plugins` directory:
```bash
cd YourProject/Plugins
git clone https://github.com/uuuuzz/UEBridgeMCP.git
```

2. Regenerate project files and build your project

3. Enable the plugin in your `.uproject` file or via Editor → Plugins

### Configuration

The plugin starts an HTTP server on `127.0.0.1:8080/mcp`. Configure in `Config/DefaultUEBridgeMCP.ini`:

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
; HTTP server port (default: 8080)
; Change this if running multiple UE instances
ServerPort=8080

; Auto-start server when editor opens
bAutoStartServer=true

; Bind address (default: 127.0.0.1 for security)
BindAddress=127.0.0.1
```

> **Tip:** When running multiple UE projects, set a different `ServerPort` for each project (e.g., 8080, 8081, 8082).

### Client Configuration

**Claude Code CLI**:
```bash
claude mcp add --transport http unreal-engine http://127.0.0.1:8080/mcp
```

**Claude Desktop** (`claude_desktop_config.json`):
```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**Cursor** (`.cursor/mcp.json`):
```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**VS Code Continue** (`.continue/config.json`):
```json
{
  "mcpServers": [{
    "name": "unreal-engine",
    "transport": { "type": "http", "url": "http://127.0.0.1:8080/mcp" }
  }]
}
```

## Available Tools

The plugin now ships a **v2 agent-first tool surface** built around:

- **summary queries** for fast discovery with small response bodies
- **detail queries** for precise follow-up inspection
- **batch mutation tools** for transactional edits
- **runtime assertions** for PIE verification

### v2 core workflow

The intended agent workflow is now:

`summary query -> targeted detail -> transactional edit -> compile/save -> assert`

This replaces the older default pattern of chaining many micro-tools such as:

- `add-graph-node`
- `connect-graph-pins`
- `disconnect-graph-pin`
- `remove-graph-node`
- `set-property`

### v2 query/detail tools

- `query-blueprint-summary`
- `query-blueprint-graph-summary`
- `query-blueprint-node`
- `query-world-summary`
- `query-level-summary`
- `query-actor-detail`
- `query-material-summary`
- `query-material-instance`

### v2 batch/assert tools

- `edit-blueprint-graph`
- `edit-blueprint-members`
- `edit-blueprint-components`
- `edit-level-batch`
- `edit-material-instance-batch`
- `assert-world-state`

### Structured response contract

All v2 tools now return a **standard MCP-first** result envelope:

- `content`
- `structuredContent`
- `isError` (only when the call failed)
- `_meta.diagnostics`
- `_meta.timing`
- `_meta.stats`

Human-readable `content.text` is intentionally kept to a short summary instead of duplicating the full payload. Machine consumers should read `structuredContent` first.

### Actor handle contract

Level/world summary tools emit actor handles using stable identity fields:

- `entity_id`: actor object path
- `resource_path`: world path
- `display_name`: human-readable label only
- `session_id`: MCP session used to create the handle

`query-actor-detail` resolves handles by `resource_path + entity_id` first and only falls back to actor names when a complete handle is not available.

### Legacy tools

Some legacy tools still exist in source as implementation helpers or compatibility shims, but the v2 tools above are the primary public interface going forward.

### Python Scripting (New in v1.10.0)

#### `run-python-script`
Execute Python scripts in Unreal Editor's Python environment.

**Parameters:**
- `script` (string, optional) - Inline Python code
- `script_path` (string, optional) - Path to Python script file
- `arguments` (object, optional) - Arguments accessible via `unreal.get_mcp_args()`

**Requirements:** PythonScriptPlugin must be enabled

**Example:**
```json
{
  "script": "import unreal\nprint(unreal.SystemLibrary.get_project_name())",
  "arguments": {"asset_path": "/Game/MyAsset"}
}
```

---

### Blueprint Analysis (7 tools)

#### `analyze-blueprint`
Complete Blueprint structure analysis including parent class, functions, variables, and components.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path (e.g., `/Game/Blueprints/BP_Character`)

**Returns:** JSON with complete Blueprint metadata

---

#### `get-blueprint-functions`
List all functions with signatures, parameters, return types, and metadata.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path
- `function_filter` (string, optional) - Filter by function name (wildcards supported)

**Returns:** Array of function definitions with full signatures

---

#### `get-blueprint-variables`
List all variables with types, default values, replication settings, and metadata.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path
- `variable_filter` (string, optional) - Filter by variable name (wildcards supported)

**Returns:** Array of variables with types and default values

---

#### `get-blueprint-components`
Get component hierarchy with transforms and attachment relationships.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path
- `include_transforms` (boolean, optional) - Include component transforms (default: true)

**Returns:** Component tree with hierarchy and transforms

---

#### `get-blueprint-graph`
Read complete Blueprint graph structure including all nodes, connections, and pin data.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path
- `graph_name` (string, optional) - Specific graph name
- `graph_type` (string, optional) - Filter: `event`, `function`, or `macro`
- `include_positions` (boolean, optional) - Include node X/Y positions (default: false)

**Returns:** All graphs with nodes, pins, and connections

---

#### `get-blueprint-node`
Get detailed information about a specific Blueprint node by GUID.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path
- `node_guid` (string, required) - Node GUID to inspect

**Returns:** Full node details with pins and connections

---

#### `get-blueprint-defaults`
Read CDO (Class Default Object) property values from Blueprint.

**Parameters:**
- `asset_path` (string, required) - Blueprint asset path
- `property_filter` (string, optional) - Filter by property name (wildcards supported)
- `category_filter` (string, optional) - Filter by property category

**Returns:** All property defaults with types, categories, and flags

---

### Level/World Tools (2 tools)

#### `query-level`
List actors in the currently open level with filtering options.

**Parameters:**
- `class_filter` (string, optional) - Filter by actor class (wildcards supported)
- `folder_filter` (string, optional) - Filter by World Outliner folder path
- `tag_filter` (string, optional) - Filter by actor tag
- `include_hidden` (boolean, optional) - Include hidden actors (default: false)
- `include_components` (boolean, optional) - Include component list (default: false)
- `include_transform` (boolean, optional) - Include transforms (default: true)
- `limit` (integer, optional) - Max results (default: 100)

**Returns:** Array of actors with optional transforms and components

---

#### `get-actor-details`
Deep inspection of a specific actor in the level.

**Parameters:**
- `actor_name` (string, required) - Actor name or label to inspect
- `include_properties` (boolean, optional) - Include all properties (default: true)
- `include_components` (boolean, optional) - Include component details (default: true)

**Returns:** Complete actor details with properties and component hierarchy

---

### Project Configuration (1 tool)

#### `get-project-settings`
Query project configuration settings.

**Parameters:**
- `section` (string, optional) - Section to query: `input`, `collision`, `tags`, `maps`, or `all` (default: `all`)

**Returns:** JSON with requested configuration sections:
- **input**: Action/axis mappings with keys and modifiers
- **collision**: Profiles, channels, and responses
- **tags**: Gameplay tag sources and settings
- **maps**: Default maps and game modes

---

### Analysis Tools (2 tools)

#### `get-class-hierarchy`
Browse class inheritance tree showing parents and children.

**Parameters:**
- `class_name` (string, required) - Class to inspect (e.g., `AActor`, `UActorComponent`)
- `direction` (string, optional) - `parents`, `children`, or `both` (default: `both`)
- `include_blueprints` (boolean, optional) - Include Blueprint subclasses (default: true)
- `depth` (integer, optional) - Max inheritance depth (default: 10)

**Returns:** Class hierarchy with parent chain and child classes

---

#### `inspect-data-asset`
Read DataTable and DataAsset contents.

**Parameters:**
- `asset_path` (string, required) - DataTable or DataAsset path
- `row_filter` (string, optional) - Filter rows by name (wildcards supported, DataTable only)

**Returns:**
- **DataTable**: All rows with field data
- **DataAsset**: All properties with values

---

### Asset Management (1 tool)

#### `search-assets`
Search assets by name, class, or path with wildcard support.

**Parameters:**
- `pattern` (string, optional) - Search pattern (wildcards supported)
- `class_filter` (string, optional) - Filter by asset class
- `path_filter` (string, optional) - Filter by asset path
- `limit` (integer, optional) - Max results (default: 100)

**Returns:** Array of matching assets with paths and types

## Architecture

```
┌──────────────────┐  HTTP POST   ┌──────────────────────────────┐
│  Claude / Cursor │ ──────────►  │  UE Editor                   │
│  Windsurf / etc  │  JSON-RPC    │  └─ UEBridgeMCP Plugin          │
│                  │ ◄──────────  │     └─ FHttpServerModule     │
│                  │   Response   │        (127.0.0.1:8080/mcp)│
└──────────────────┘              └──────────────────────────────┘
```

**Modules:**
- `UEBridgeMCP` (Runtime) - Core MCP protocol layer (JSON-RPC, tool registry)
- `UEBridgeMCPEditor` (Editor) - HTTP server + UE Editor tool implementations

## Development

### Adding Custom Tools

Custom tools can be registered by subclassing `UMcpToolBase` (declared in
`Source/UEBridgeMCP/Public/McpToolBase.h`). Override `GetToolName`,
`GetToolDescription`, `GetInputSchema`, and `Execute`, then register the tool
with `FMcpToolRegistry::Get().RegisterTool<YourTool>()` during module startup.

The built-in tools under `Source/UEBridgeMCPEditor/Private/Tools/` serve as
reference implementations.

### Building

Requires Unreal Engine 5.6 or higher.

```bash
# Build with UnrealBuildTool
<UE>/Engine/Build/BatchFiles/Build.bat YourProjectEditor Win64 Development
```

## License

GNU General Public License v3.0 - see [LICENSE](LICENSE) for details

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.

## Links

- [GitHub Repository](https://github.com/uuuuzz/UEBridgeMCP)
- [Changelog](CHANGELOG.md)
- [Model Context Protocol](https://modelcontextprotocol.io)
- [MCP Specification](https://modelcontextprotocol.io/specification)

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=uuuuzz/UEBridgeMCP&type=Date)](https://star-history.com/#uuuuzz/UEBridgeMCP&Date)