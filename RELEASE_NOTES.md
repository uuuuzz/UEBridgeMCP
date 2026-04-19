# UEBridgeMCP v1.19.0 — First Public Release 🎉

**Native C++ Model Context Protocol (MCP) plugin for Unreal Engine 5.6+.**

UEBridgeMCP exposes the Unreal Editor as a first-class MCP server running
directly inside the engine — no external Python bridge, no sidecar process,
no extra dependencies.

## ✨ Highlights

- 🚀 **Zero-dependency native plugin** — uses UE's built-in `FHttpServerModule`.
- 🤝 **Works with every major MCP client** — Claude Code CLI, Claude Desktop,
  Cursor, Windsurf, VS Code Continue, OpenAI / Copilot, and any Streamable
  HTTP client.
- 🎯 **Agent-first v2 tool surface** — designed around the workflow
  `summary → detail → batch edit → compile/save → assert`, keeping response
  payloads small and predictable.
- 🧩 **Structured response contract** — every tool returns
  `content` + `structuredContent` + `_meta.diagnostics / timing / stats`.
- 🔑 **Stable actor handles** — `entity_id` + `resource_path` let agents
  reference actors across calls without relying on fragile display names.
- 🐍 **Python scripting built in** — run inline Python or `.py` files inside
  the Editor via `run-python-script`, with arguments exposed through
  `unreal.get_mcp_args()`.
- 🔧 **Extensible** — subclass `UMcpToolBase` and register with
  `FMcpToolRegistry` to ship your own custom tools.

## 📦 Installation

```bash
cd YourProject/Plugins
git clone https://github.com/uuuuzz/UEBridgeMCP.git
```

Then regenerate project files and rebuild. Enable the plugin in your
`.uproject` or via **Editor → Plugins**.

Default server endpoint: `http://127.0.0.1:8080/mcp`.

## 🛠️ What's Included

**v2 query / detail tools** — `query-blueprint-summary`,
`query-blueprint-graph-summary`, `query-blueprint-node`,
`query-world-summary`, `query-level-summary`, `query-actor-detail`,
`query-material-summary`, `query-material-instance`.

**v2 batch / assert tools** — `edit-blueprint-graph`,
`edit-blueprint-members`, `edit-blueprint-components`, `edit-level-batch`,
`edit-material-instance-batch`, `assert-world-state`.

**Classic tooling** — Blueprint analysis (7 tools), Level / World inspection,
project configuration readout, class hierarchy browser, DataTable /
DataAsset inspector, asset search, and Python scripting.

See the full list and parameters in [README.md](README.md#available-tools).

## ⚠️ Compatibility & Status

- Requires **Unreal Engine 5.6 or newer** (targets UE's public C++ API from
  5.6; older versions are not supported).
- Marked **experimental** — public API may still evolve before 2.0.
- Server binds to `127.0.0.1` by default. Change `BindAddress` in
  `Config/DefaultUEBridgeMCP.ini` only if you understand the security
  implications.

## 🙏 Feedback

Bug reports and feature requests are very welcome on
[GitHub Issues](https://github.com/uuuuzz/UEBridgeMCP/issues).

Full change list: [CHANGELOG.md](CHANGELOG.md).
