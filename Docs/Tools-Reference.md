# UEBridgeMCP ? Tools Reference (46 built-in tools)

> Authoritative list ? every row below appears exactly once in
> `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp :: RegisterBuiltInTools()`.
> For the machine-readable full JSON Schema of each tool, query the running editor:
>
> ```bash
> curl -s -X POST http://127.0.0.1:8080/mcp \
>   -H "Content-Type: application/json" \
>   -H "Accept: application/json,text/event-stream" \
>   -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
> ```
>
> This document is the human-facing cheat sheet. The editor is the source of truth.

## Table of Contents

1. [Blueprint Query (3)](#1-blueprint-query-3)
2. [Level / World Query (3)](#2-level--world-query-3)
3. [Material Query (2)](#3-material-query-2)
4. [Project / Asset / Utility Query (7)](#4-project--asset--utility-query-7)
5. [Create / Utility Write (3)](#5-create--utility-write-3)
6. [StateTree (5)](#6-statetree-5)
7. [Scripting (1)](#7-scripting-1)
8. [Build (2)](#8-build-2)
9. [PIE ? Play-In-Editor (4)](#9-pie--play-in-editor-4)
10. [Reflection RPC (1)](#10-reflection-rpc-1)
11. [Batch Edit (6)](#11-batch-edit-6)
12. [Asset Lifecycle & Validation (5)](#12-asset-lifecycle--validation-5)
13. [High-Level Orchestration (4)](#13-high-level-orchestration-4)

Total: **46** tools (3 + 3 + 2 + 7 + 3 + 5 + 1 + 2 + 4 + 1 + 6 + 5 + 4).

---

## Conventions

- All requests are JSON-RPC 2.0 over HTTP POST to `http://127.0.0.1:8080/mcp`.
- Every tool call uses MCP's standard `tools/call` method:
  ```json
  {"jsonrpc":"2.0","id":1,"method":"tools/call",
   "params":{"name":"<tool-name>","arguments":{ ... }}}
  ```
- Paths:
  - Asset paths use UE object paths ? `/Game/Blueprints/BP_Hero` (no extension).
  - World / level paths ? `/Game/Maps/TestMap`.
- Error envelope: `{"isError": true, "content":[{"type":"text","text":"UEBMCP_<CATEGORY>: <msg>"}]}`.
- Successful responses always carry a structured `content[0].text` JSON string and `isError:false`.

> **Note on v1 vs v2 tools** ? several older tools (`query-blueprint`, `query-blueprint-graph`, `query-level`, `query-material`, `set-property`, `add-graph-node`, `spawn-actor`, ?) still exist in the source tree but are **no longer registered**. They were absorbed into the v2 batch tools below (`edit-blueprint-graph`, `edit-level-batch`, `edit-material-instance-batch`, the `query-*-summary` family). The running server will tell you exactly what is live via `tools/list`.

---

## 1. Blueprint Query (3)

| Tool | Purpose | Key args |
|---|---|---|
| `query-blueprint-summary` | Compact BP summary with counts for graphs, functions, variables, components | `blueprint_path` |
| `query-blueprint-graph-summary` | List all graphs (Event / Function / Macro) in a BP with node counts | `blueprint_path` |
| `query-blueprint-node` | Deep info for a single node incl. object/class pin defaults | `blueprint_path`, `node_guid` |

**Example**
```json
{"name":"query-blueprint-summary",
 "arguments":{"blueprint_path":"/Game/Blueprints/BP_Hero"}}
```

---

## 2. Level / World Query (3)

| Tool | Purpose | Key args |
|---|---|---|
| `query-level-summary` | Compact counts-and-buckets view of the current level | `world_type` (`editor`\|`pie`) |
| `query-actor-detail` | Detailed reflected info for a specific actor (components, properties, tags) | `actor_name` or `actor_label` |
| `query-world-summary` | Streaming level structure, world composition, gameplay settings | `include[]` |

**Example**
```json
{"name":"query-actor-detail","arguments":{"actor_label":"BP_Hero_C_0"}}
```

---

## 3. Material Query (2)

| Tool | Purpose | Key args |
|---|---|---|
| `query-material-summary` | Compact overview of a `Material` asset: shading model, domain, blend mode, param counts | `material_path` |
| `query-material-instance` | Inspect `MaterialInstanceConstant` / `Dynamic` overrides, parent chain, scalar / vector / texture params | `material_instance_path` |

**Example**
```json
{"name":"query-material-instance",
 "arguments":{"material_instance_path":"/Game/Materials/MI_Hero_Red"}}
```

---

## 4. Project / Asset / Utility Query (7)

| Tool | Purpose | Key args |
|---|---|---|
| `get-project-info` | Project / engine / modules / plugins / target platforms | *(none)* |
| `query-asset` | Content Browser search + DataTable / DataAsset inspection | `pattern`, `class_filter`, `path`, `inspect` |
| `get-asset-diff` | Structured diff of a binary asset (BP / Material / DT) against SCM base | `asset_path`, `revision` |
| `get-class-hierarchy` | Parent / child class trees (C++ & Blueprint) | `class_name`, `direction` (`up`\|`down`\|`both`) |
| `find-references` | Find what references an asset or what it references | `asset_path`, `direction` |
| `inspect-widget-blueprint` | UMG Widget BP tree, bindings, animations, input mappings | `widget_path` |
| `get-logs` | Read UE output-log buffer with category / text / level filters | `category`, `contains`, `limit`, `level` |

**Example**
```json
{"name":"get-logs","arguments":{"category":"LogBlueprint","limit":100,"level":"Warning"}}
```

---

## 5. Create / Utility Write (3)

| Tool | Purpose | Key args |
|---|---|---|
| `create-asset` | Create a new Blueprint / Material / DataTable / Level / etc. | `class_name`, `target_path`, `parent_class` |
| `add-widget` | Add a widget to a UMG Widget BP tree | `widget_path`, `widget_class`, `parent_slot_name` |
| `add-datatable-row` | Add or update a row in a DataTable asset | `datatable_path`, `row_name`, `fields{}` |

**Example**
```json
{"name":"create-asset",
 "arguments":{"class_name":"Blueprint","target_path":"/Game/Blueprints/BP_NewActor",
              "parent_class":"/Script/Engine.Actor"}}
```

---

## 6. StateTree (5)

| Tool | Purpose | Key args |
|---|---|---|
| `query-statetree` | Inspect a StateTree asset ? states, tasks, transitions | `statetree_path` |
| `add-statetree-state` | Add a new state under a parent state | `statetree_path`, `parent_state_id`, `state_name` |
| `remove-statetree-state` | Remove a state (and its subtree) | `statetree_path`, `state_id` |
| `add-statetree-transition` | Add a transition between two states | `statetree_path`, `from_state_id`, `to_state_id`, `trigger` |
| `add-statetree-task` | Add a task node to a state | `statetree_path`, `state_id`, `task_class`, `properties{}` |

**Example**
```json
{"name":"add-statetree-transition",
 "arguments":{"statetree_path":"/Game/AI/ST_Enemy",
              "from_state_id":"Patrol","to_state_id":"Chase",
              "trigger":"OnEnterCondition"}}
```

---

## 7. Scripting (1)

| Tool | Purpose | Key args |
|---|---|---|
| `run-python-script` | Execute inline Python or a `.py` file inside UE's embedded interpreter | `command` **or** `script_path`, `capture_output` |

Returns `stdout`, `stderr`, and any `AsyncTask`-spawned log lines. Python errors are caught and returned as structured errors ? the editor does **not** crash (see [Troubleshooting](./Troubleshooting.md)).

**Example**
```json
{"name":"run-python-script",
 "arguments":{"command":"import unreal\nprint(unreal.SystemLibrary.get_engine_version())"}}
```

---

## 8. Build (2)

| Tool | Purpose | Key args |
|---|---|---|
| `trigger-live-coding` | Fire Live Coding compile (Ctrl+Alt+F11 equivalent); optional `wait_for_completion` | `wait_for_completion` (bool), `timeout_seconds` |
| `build-and-relaunch` | Close **this** editor instance (identified by PID), rebuild, relaunch | `target_configuration`, `include_editor` |

`build-and-relaunch` only closes the MCP-connected editor instance; other open editors are untouched. Windows-only.

**Example**
```json
{"name":"trigger-live-coding","arguments":{"wait_for_completion":true,"timeout_seconds":60}}
```

---

## 9. PIE ? Play-In-Editor (4)

| Tool | Purpose | Key args |
|---|---|---|
| `pie-session` | **Fire-and-forget** start / stop / pause / resume PIE. Never blocks the game thread. | `action` (`start`\|`stop`\|`pause`\|`resume`), `mode` |
| `pie-input` | Inject keys / axes / action-move-to into a running PIE world | `event` (`key`\|`axis`\|`move-to`), `key`, `axis`, `value`, `target_location` |
| `wait-for-world-condition` | Poll a JSON-defined world condition with timeout (actor count, property equals, ?) | `condition{}`, `timeout_seconds`, `poll_interval_ms` |
| `assert-world-state` | One-shot assertion used by tests (returns pass/fail JSON) | `assertions[]` |

> **Known pitfall** ? on UE 5.6 + legacy `AxisMapping`, injected `key:W` / `axis:MoveForward` reach `UPlayerInput::InputKey` but may **not** be sampled by BP `InputAxis` nodes. Prefer `event:move-to` (AI pathfind) for end-to-end movement validation, or call `AddMovementInput` directly via `call-function`. See [Troubleshooting](./Troubleshooting.md) for the full story.

**Example**
```json
{"name":"pie-session","arguments":{"action":"start","mode":"SelectedViewport"}}
```

Follow up with `wait-for-world-condition` or `query-gameplay-state` for readiness ? don't busy-loop `pie-session`.

---

## 10. Reflection RPC (1)

| Tool | Purpose | Key args |
|---|---|---|
| `call-function` | Invoke any `BlueprintCallable` UFUNCTION on an actor, CDO, or subsystem, with reflected arg marshaling | `target`, `function`, `args{}`, `world_type` |

`target` accepts an actor name, `/Script/Module.Class` (for CDO), `/Game/...` (for BP CDO), or a subsystem path.

**Example**
```json
{"name":"call-function",
 "arguments":{"target":"BP_GameMode_C_0","function":"SetDifficulty","args":{"Level":3}}}
```

---

## 11. Batch Edit (6)

The v2 core ? replaces the legacy single-operation tools. Every batch tool supports **transactions** (single `FScopedTransaction` for undo), **dry-run validation**, and optional **compile + save** in the same call.

| Tool | Purpose | Key args |
|---|---|---|
| `edit-blueprint-graph` | Transactional graph edits: add / remove / connect / disconnect nodes with alias-based cross-referencing | `blueprint_path`, `graph_name`, `operations[]`, `dry_run`, `compile`, `save` |
| `edit-blueprint-members` | Create / rename / delete variables, functions, event dispatchers | `blueprint_path`, `operations[]` |
| `edit-blueprint-components` | Edit SCS: add / remove / rename / attach / set-root / set-defaults | `blueprint_path`, `operations[]` |
| `edit-level-batch` | Batched level mutations (spawn / delete / transform / attach / detach / duplicate) | `operations[]`, `world_type` |
| `edit-material-instance-batch` | Batched param set on one or more `MaterialInstanceConstant` assets | `operations[]` |
| `compile-assets` | Compile one or more `Blueprint / WidgetBlueprint / AnimBlueprint` assets | `asset_paths[]`, `save_on_success` |

**Example ? build an EventGraph "Print on BeginPlay" in one round-trip**
```json
{"name":"edit-blueprint-graph",
 "arguments":{
   "blueprint_path":"/Game/BP_Hero","graph_name":"EventGraph",
   "compile":true,"save":true,
   "operations":[
     {"op":"add_node","alias":"print","class":"K2Node_CallFunction",
      "properties":{"FunctionReference":{"MemberName":"PrintString"}}},
     {"op":"connect","from_alias":"BeginPlay","from_pin":"then",
      "to_alias":"print","to_pin":"execute"}
   ]}}
```

---

## 12. Asset Lifecycle & Validation (5)

| Tool | Purpose | Key args |
|---|---|---|
| `manage-assets` | Batched rename / move / duplicate / delete / save | `actions[]` |
| `import-assets` | Import external files (FBX / PNG / JPG / TGA / WAV / ?) | `files[]`, `destination_path`, `options{}` |
| `source-control-assets` | SCM ops: status, checkout, revert, submit, sync | `action`, `asset_paths[]` |
| `capture-viewport` | Screenshot editor viewport, PIE window, or a specific panel | `source` (`editor`\|`pie`\|`panel`), `output_path`, `width`, `height` |
| `apply-material` | Apply a material / MIC to actor(s) by name, label, or class | `target`, `material_path`, `slot_index` |

**Example**
```json
{"name":"capture-viewport",
 "arguments":{"source":"pie","output_path":"D:/tmp/shot.png","width":1920,"height":1080}}
```

---

## 13. High-Level Orchestration (4)

| Tool | Purpose | Key args |
|---|---|---|
| `blueprint-scaffold-from-spec` | Create or merge a BP from a JSON spec (components, variables, functions, event-graph steps) ? higher-level than `create-asset` + `edit-blueprint-*` | `target_path`, `spec{}`, `dry_run` |
| `query-gameplay-state` | Snapshot of PIE gameplay: PlayerController, Pawn, GameState, GameMode, world time | `world_type`, `include[]` |
| `auto-fix-blueprint-compile-errors` | Apply deterministic repair strategies to BP compile errors (reconnect dropped pins, replace missing node refs, ?) | `blueprint_path`, `strategies[]`, `dry_run` |
| `generate-level-structure` | Generate a level skeleton (folders, sublevels, world settings) from a declarative spec | `map_path`, `spec{}` |

**Example**
```json
{"name":"blueprint-scaffold-from-spec",
 "arguments":{
   "target_path":"/Game/Blueprints/BP_Minion",
   "spec":{
     "parent_class":"/Script/Engine.Character",
     "components":[{"name":"Health","class":"UHealthComponent"}],
     "variables":[{"name":"MaxHP","type":"float","default":100.0}]
   }}}
```

---

## Checking the live inventory

The authoritative, machine-readable list is always what the running editor reports:

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools | length'
```

Expect **46**. If you see a different number on your local build, your plugin copy is out of date ? pull the latest `main` and rebuild.

---

See also:

- [Tool Development Guide](./ToolDevelopment.md) ? how to author a new tool
- [Architecture](./Architecture.md) ? module layout, server, registry, protocol
- [Troubleshooting](./Troubleshooting.md) ? IPv6, ports, PIE deadlock, Python GC, pie-input AxisMapping gotcha
