# UEBridgeMCP ? Troubleshooting

Practical fixes for the problems you will actually hit. If your symptom isn't listed, collect the `Saved/Logs/<Project>.log` file around the time of failure ? the plugin logs under `LogUEBridgeMCP` and `LogUEBridgeMCPEditor`.

---

## 1. Connectivity

### 1.1 Client reports `ECONNREFUSED` on Windows

**Cause.** Windows resolves `localhost` to IPv6 (`[::1]`) first, while the plugin binds to IPv4 (`127.0.0.1`) by default.

**Fix.** Always use `127.0.0.1` in client configs:

```jsonc
// claude_desktop_config.json
{ "mcpServers": { "unreal-engine": { "url": "http://127.0.0.1:8080/mcp" } } }
```

### 1.2 Server not running at all

- The editor is closed, or the plugin is disabled. Open `Edit -> Plugins`, search "UEBridgeMCP", and make sure the plugin and its editor modules are enabled.
- Port already bound by another process. On Windows:
  ```powershell
  netstat -ano | findstr 8080
  ```
  If something else owns 8080, either kill it or change `ServerPort` in `Config/DefaultUEBridgeMCP.ini`.

### 1.3 Port clash between multiple UE instances

Each editor instance needs its **own** port. Give each project a unique `ServerPort` (8080 / 8081 / 8082) in `Config/DefaultUEBridgeMCP.ini` or the editor settings object, then update the client's URL per project.

### 1.4 `tools/list` returns empty or 404

- The editor module failed to load. Check `LogUEBridgeMCPEditor` at startup for errors.
- `RegisterBuiltInTools` ran before dependent modules were ready. The plugin uses `LoadingPhase = PostEngineInit` on purpose ? don't change that.
- You built with `WITH_EDITOR=0`. The HTTP server lives in the Editor module only.

---

## 2. Build & Module Loading

### 2.1 Plugin refuses to build on a fresh project

Missing engine plugin dependencies. UE should auto-enable them via the plugin's `.uplugin`, but if it doesn't, add these to your `.uproject`:

- `EditorScriptingUtilities`
- `GameplayAbilities`
- `EnhancedInput`
- `StateTree` + `GameplayStateTree`
- `PythonScriptPlugin` (hard linked in `Build.cs`)
- `Niagara` and `Metasound` if you keep the current conditional tool sources enabled
- `ControlRig` and `PCG` if you keep the extension modules enabled

Then Generate Project Files, rebuild.

### 2.2 "UEBridgeMCPEditor.generated.h not found"

Regenerate project files ? right-click your `.uproject` -> Generate Visual Studio project files, or run `UnrealBuildTool -projectfiles`.

### 2.3 Live Coding changes aren't reflected

- Live Coding wasn't actually triggered. Press `Ctrl+Alt+F11` in the editor, or call the `trigger-live-coding` tool with `wait_for_completion: true`.
- The patch failed. Look at `Saved/Logs/UnrealEditor.log` for the "Patch failed" message. Common culprits: changed a header in a way that breaks ABI (added virtual, changed class size, added `UPROPERTY`) ? requires a full rebuild.

---

## 3. Python Scripting

### 3.1 `run-python-script` returns `SyntaxError: expected an indented block`

**Root cause (already fixed, do not regress).** `ExecutePython` wraps user code in a `try/except` block. The original indenter used `Command.Replace("\n", "\n    ")`, which only indented lines **after** the first newline ? the first line stayed flush against `try:`, triggering a `SyntaxError`.

**Fix.** Prepend `    ` to the first line, **then** replace:

```cpp
FString IndentedCommand = TEXT("    ") + Command.Replace(TEXT("\n"), TEXT("\n    "));
```

File: `Plugins/UEBridgeMCP/Source/UEBridgeMCPEditor/Private/Tools/Scripting/RunPythonScriptTool.cpp`.

### 3.2 Editor crashes after Python exception

**Root cause (already fixed, stop-gap).** The unconditional `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)` after `ExecPythonCommand` could crash inside `GetTypeHash(FUtf8String) -> CodepointFromUtf8` when a Python error put GC in an inconsistent state.

**Fix.** Removed the blanket GC call from `ExecutePython()`. Only the top-level `Execute()` runs GC, and only when it detected a level load (`bLevelLoadDetected == true`).

### 3.3 Python cannot `import unreal`

That means the `PythonScriptPlugin` is disabled. Re-enable it via `Edit -> Plugins -> Scripting -> Python Editor Script Plugin`. Restart the editor.

---

## 4. PIE (Play-In-Editor)

### 4.1 `pie-session start` or `pie-session stop` never returns

**Root cause (already fixed).** The original `ExecuteStart` / `ExecuteStop` blocked the game thread waiting for PIE readiness (`WaitForPIEReady`, `while + FPlatformProcess::Sleep`). PIE cannot tick while the game thread is blocked, so it can never become ready ? classic deadlock; the HTTP request hangs forever.

**Fix.** After `RequestPlaySession` / `RequestEndPlayMap`, return immediately with `"status":"starting"` / `"stopping"`. The client is expected to poll:

```json
{"name":"wait-for-world-condition",
 "arguments":{"condition":{"type":"pie_state","equals":"Playing"},
              "timeout_seconds":10}}
```

or `query-gameplay-state` if you want a one-shot snapshot.

File: `Plugins/UEBridgeMCP/Source/UEBridgeMCPEditor/Private/Tools/PIE/PieSessionTool.cpp`.

### 4.2 `pie-input key:W` reaches the engine but the character doesn't move

**Verified symptom** on UE 5.6 + Third Person Template + **legacy `AxisMapping`**: the injection path `PlayerController->InputKey` -> `UPlayerInput::InputKey` -> `KeyStateMap` is wired correctly, but:

- Blueprint `InputAxis MoveForward` nodes do not sample the synthesized event.
- `GetInputKeyTimeDown` and `GetInputAxisValue` both return `0`.
- The pawn doesn't move.

**Likely cause.** PIE world tick ordering and the `InputComponent` stack differ enough from real input that the simulated key never reaches the AxisMapping sampler.

**What to do instead.** For end-to-end character movement validation, either:

1. Use `pie-input` with `event:"move-to"` (AI pathfinding):
   ```json
   {"name":"pie-input",
    "arguments":{"event":"move-to","target_location":[1000,0,0]}}
   ```
2. Drive the pawn directly via `call-function`:
   ```json
   {"name":"call-function",
    "arguments":{"target":"ThirdPersonCharacter_C_0",
                 "function":"AddMovementInput",
                 "args":{"WorldDirection":{"X":1,"Y":0,"Z":0},"ScaleValue":1.0}}}
   ```

Reserve `pie-input key:X` for UI buttons and verified Enhanced Input bindings.

### 4.3 PIE state is stale between runs

`assert-world-state` or `wait-for-world-condition` still sees the last PIE's actors. Make sure you called `pie-session stop` and polled until `Stopped`; the editor sometimes takes a full tick to tear down streamed levels.

---

## 5. Asset & Blueprint Operations

### 5.1 `create-asset` fails with `UEBMCP_INVALID_PATH`

- Paths must start with `/Game/` (not `Game/`, not `Content/`, not a Windows path).
- The target directory must already exist or be creatable. Use `manage-assets` with action `ensure_directory` to create it first.

### 5.2 `edit-blueprint-graph` returns `UEBMCP_COMPILE_FAILED`

Call the tool with `"compile": false, "save": false` first to get a clean batch result, then inspect `get-logs` with `category: LogBlueprint, level: Error` for the compiler diagnostics. Common culprits: dropped input pins, missing parent class, renamed variables still referenced by other graphs.

If the errors look structural, `auto-fix-blueprint-compile-errors` can often repair them:

```json
{"name":"auto-fix-blueprint-compile-errors",
 "arguments":{"blueprint_path":"/Game/BP_Hero","strategies":["reconnect_exec","replace_missing_refs"]}}
```

### 5.3 Old tool names come back as "tool not found"

The legacy v1 tools were consolidated in v2:

| Old (v1) | New (v2) |
|---|---|
| `query-blueprint` | `query-blueprint-summary` + `query-blueprint-node` |
| `query-blueprint-graph` | `query-blueprint-graph-summary` + `query-blueprint-node` |
| `query-level` | `query-level-summary` + `query-actor-detail` |
| `query-material` | `query-material-summary` |
| `spawn-actor` / `set-property` / `add-component` | `edit-level-batch` |
| `add-graph-node` / `remove-graph-node` / `connect-graph-pins` / `disconnect-graph-pin` | `edit-blueprint-graph` for Blueprint graphs, `edit-material-graph` for `UMaterial` expression graphs |
| `set-property` (on Blueprint members) | `edit-blueprint-members` |
| `analyze-blueprint` | `query-blueprint-summary` |

If your client has cached the old schema, restart it so it pulls `tools/list` again.

---

## 6. Migration & Source Control

### 6.1 Moving the plugin to a new project

UEBridgeMCP has **zero** game-code coupling ? no `Lyra*` / project-specific symbols in `Source/`. Steps:

1. Copy `UEBridgeMCP/` to `<NewProject>/Plugins/` (exclude `Binaries/`, `Intermediate/`).
2. Make sure the new project is C++ and UE 5.6+.
3. Add `{"Name":"UEBridgeMCP","Enabled":true}` to `.uproject`.
4. Regenerate project files, rebuild.
5. `curl -X POST http://127.0.0.1:8080/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'` to verify. After Step 6, do not expect a fixed count; compare against `initialize.capabilities.tools.registeredCount` instead.

### 6.2 Submitting via Perforce / Git

Blueprints, Materials, and DataTables are binary. Use `source-control-assets`:

```json
{"name":"source-control-assets",
 "arguments":{"action":"checkout","asset_paths":["/Game/BP_Hero"]}}
```

For Git, the plugin respects the editor's configured Git LFS setup ? no extra wiring needed in the plugin.

---

## 7. Observability

### 7.1 Increase log verbosity

`Config/DefaultUEBridgeMCP.ini`:

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
LogLevel=Verbose
```

Restart the editor. Per-request traces will appear under `LogUEBridgeMCPEditor`.

### 7.2 Capture all MCP traffic

Use `curl` with `-i` to see the JSON-RPC envelope:

```bash
curl -i -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

There is no separate session-log environment variable in the current source; use `LogLevel=Verbose` plus `curl -i` when you need full request/response evidence.

---

See also:

- [Tools Reference](./Tools-Reference.md) ? the live base and conditional tool surface.
- [Tool Development Guide](./ToolDevelopment.md) ? author a new tool.
- [Architecture](./Architecture.md) ? HTTP / Registry / Session internals.
