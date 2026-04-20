# UEBridgeMCP ? Architecture

How the 46 built-in tools, the HTTP server, the registry, and the session manager fit together inside a UE editor process.

---

## Big picture

```
+---------------------------------+   HTTP POST /mcp   +---------------------------------------+
| MCP client                      | ---JSON-RPC 2.0--> | UE editor process                     |
|  Claude Desktop / Claude Code   |                    |                                       |
|  Cursor / Windsurf / Continue   | <--- Response ---- |  +- UEBridgeMCP          (Runtime)    |
|  Codex / custom scripts / curl  |                    |  |   McpToolBase / Result / Registry  |
+---------------------------------+                    |  |   Schema / Protocol types          |
                                                       |  +- UEBridgeMCPEditor    (Editor)     |
                                                       |      McpServer (FHttpServerModule)    |
                                                       |      McpEditorSubsystem (lifecycle)   |
                                                       |      46 x UMcpToolBase subclasses     |
                                                       |      Toolbar icon + log capture       |
                                                       +---------------------------------------+
```

- **Transport.** `FHttpServerModule` binds `127.0.0.1:8080` by default. No external dependencies ? no Node.js / no Python server / no extra ports.
- **Protocol.** MCP 2025-03-26 over Streamable HTTP, JSON-RPC 2.0 framing. The server replies with either a plain `application/json` response or an `text/event-stream` stream for long-running calls.
- **Thread model.** HTTP requests land on worker threads. Every tool that touches UE object APIs is marshaled onto the game thread via `AsyncTask(ENamedThreads::GameThread, ...)` + `TPromise<FMcpToolResult>`. Pure read-only, thread-safe tools can opt out by returning `false` from `RequiresGameThread()`.

---

## Modules

### `UEBridgeMCP` (Runtime)

Load phase: `Default`.

Contains the small, engine-agnostic surface that both server and tools depend on:

```
Source/UEBridgeMCP/
  Public/
    UEBridgeMCP.h                   # version constant + log category
    Protocol/
      McpTypes.h                    # FJsonRpcRequest / FJsonRpcResponse / FMcpToolResult
      McpCapabilities.h             # server-advertised capability struct
    Tools/
      McpToolBase.h                 # UCLASS abstract base for every tool
      McpToolRegistry.h             # thread-safe map ToolName -> class/instance
      McpToolResult.h               # Success / Error / Streaming factories
      McpSchemaProperty.h           # JSON-Schema helper builders
```

The registry is a singleton ? `FMcpToolRegistry::Get()`. It exposes:

- `RegisterToolClass(UClass*)` ? register by class, instantiated on demand.
- `FindTool(FString Name)` ? returns the cached instance (pre-instantiated by `WarmupAllTools()` to avoid `NewObject` on worker threads).
- `GetToolCount()`, `GetAllTools()` ? for `tools/list`.

### `UEBridgeMCPEditor` (Editor)

Load phase: **`PostEngineInit`**. This is intentional ? the registry-population code depends on engine subsystems that aren't available at `Default`.

```
Source/UEBridgeMCPEditor/
  Public/
    Server/McpServer.h              # thin wrapper over FHttpServerModule
    Subsystem/McpEditorSubsystem.h  # UEditorSubsystem: lifecycle + settings
    UI/McpToolbarExtension.h        # editor toolbar status icon
    Tools/<Category>/...            # 46 tool headers
  Private/
    UEBridgeMCPEditor.cpp           # RegisterBuiltInTools() - the authoritative tool list
    Server/McpServer.cpp            # route handlers for /mcp, /mcp/session/*
    Server/McpSessionManager.cpp    # sticky per-client session IDs
    Subsystem/McpEditorSubsystem.cpp
    Tools/<Category>/*.cpp          # 46 tool implementations
```

---

## Request lifecycle

```
Client: POST /mcp
  body: {"jsonrpc":"2.0","id":42,"method":"tools/call",
         "params":{"name":"edit-blueprint-graph","arguments":{...}}}
         
        v
[FHttpServerModule worker thread]
  McpServer::HandleRequest
    -> parse JSON-RPC envelope
    -> session lookup (Mcp-Session-Id header or new)
    -> method dispatch:
         initialize   -> FMcpServer::HandleInitialize
         tools/list   -> iterate Registry, emit schemas
         tools/call   -> McpServer::HandleToolCall

        v
[ExecuteTool]
  tool = Registry.FindTool(name)
  if tool->RequiresGameThread():
      AsyncTask(GameThread, [tool, args]{
         Promise.SetValue(tool->Execute(args, ctx))
      })
      result = Promise.GetFuture().Get(timeout)
  else:
      result = tool->Execute(args, ctx)   // stays on worker thread

        v
[back on worker thread]
  McpServer::WriteResponse
    -> envelope into {result:{content:[{type:text,text:...}], isError:false}}
    -> serialise, send HTTP response
```

### Why the game-thread marshal

UE's object APIs (`UObject::Modify`, asset loading, Blueprint compilation, PIE control, SCS editing, ?) are **not** thread-safe. Without this marshal the HTTP thread would race with the editor's main loop, resulting in anything from corrupted JSON output to editor crashes.

### Why pre-instantiate

`NewObject<UMcpToolBase>` must run on the game thread ? creating a tool instance lazily on a worker thread would itself require a marshal. `FMcpToolRegistry::WarmupAllTools()` is called from `RegisterBuiltInTools()` (which itself runs on the game thread during `PostEngineInit`) to pre-create every tool and `AddToRoot()` it, keeping worker-thread `FindTool` wait-free.

---

## Session manager

The MCP spec lets clients maintain sticky sessions via the `Mcp-Session-Id` header. The server keeps a small in-memory map of `session_id -> FMcpSession` for progress notifications and cancellation support.

- A new session is created on `initialize`. The server returns `Mcp-Session-Id` in the response headers.
- Subsequent requests for the same session SHOULD carry that header.
- Cancellation: `DELETE /mcp/session/<id>` terminates in-flight work and releases session state.

Stateless clients (like a quick `curl`) can skip the header ? every call gets an ephemeral session.

---

## Configuration surface

`Config/DefaultUEBridgeMCP.ini`:

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
ServerPort=8080            ; int32
bAutoStartServer=true      ; bool
BindAddress=127.0.0.1      ; FString (use 0.0.0.0 to expose externally)
LogLevel=Log               ; Error | Warning | Log | Verbose | VeryVerbose
```

Overridable at runtime via `UMcpEditorSubsystem::GetSettings()` ? useful for automated tests that spin up multiple editors on different ports.

Environment variable `SOFT_UE_BRIDGE_PORT` is honored only as a launch-time override when present.

---

## Tool categorization

Every tool answers two optional classification questions in addition to the four required ones:

```cpp
virtual FString GetToolKind() const;       // "query" | "batch" | "detail" | "mutation"
virtual FString GetResourceScope() const;  // "blueprint" | "blueprint_graph" | "level" | ...
```

These aren't enforced ? they're used by:

- The `tools/list` response (clients with good UIs group by kind).
- Observability ? the server tags log lines with `kind=batch scope=blueprint_graph` so you can grep a session for all blueprint-graph batch operations.

---

## Error envelope

All tool errors flow through a single path:

```cpp
return FMcpToolResult::Error(TEXT("UEBMCP_NOT_FOUND: blueprint /Game/... does not exist"));
```

Which becomes:

```json
{"jsonrpc":"2.0","id":42,
 "result":{"content":[{"type":"text","text":"UEBMCP_NOT_FOUND: blueprint /Game/... does not exist"}],
           "isError":true}}
```

MCP clients that follow the spec will surface this as a tool failure (with `isError: true`), not a transport-level error ? which means the AI assistant can see the message and retry with better arguments.

---

## Server lifecycle

```
Editor launch
   v
UEBridgeMCPEditor module load  (PostEngineInit)
   v
FUEBridgeMCPEditorModule::StartupModule()
   RegisterBuiltInTools()      -> populate FMcpToolRegistry
     Registry.WarmupAllTools() -> pre-instantiate all 46
   FMcpToolbarExtension::Initialize()
   v
FUEBridgeMCPEditorModule owns UMcpEditorSubsystem
   v
UMcpEditorSubsystem::Initialize()
   if bAutoStartServer: McpServer::Start(port)
   v
[Editor runs]
   v
Editor shutdown
   v
UMcpEditorSubsystem::Deinitialize()
   McpServer::Stop()
   v
FUEBridgeMCPEditorModule::ShutdownModule()
   FMcpToolbarExtension::Shutdown()
```

`McpServer::Start` uses `FHttpServerModule::Get().GetHttpRouter(Port)` and registers handlers for `/mcp` (POST, JSON-RPC), `/mcp/session/*` (DELETE, cancellation), and `/health` (GET, liveness check used by CI).

---

## What this plugin does **not** try to be

- **Not a game runtime.** The server is editor-only ? the Editor module is the one exposing the HTTP endpoint. Packaged builds do not include it.
- **Not a permission system.** Any client that can reach `127.0.0.1:8080` has full tool access. Don't expose the port on public interfaces without an upstream reverse proxy doing auth.
- **Not a replacement for Unreal Automation.** Automation Tests own the committed regression story. This plugin is for **exploration** ? quickly prodding the editor from an AI agent, collecting findings, and converting the keepers into committed `FAutomationSpecBase` tests.

---

See also:

- [Tool Development Guide](./ToolDevelopment.md) ? author a new tool.
- [Tools Reference](./Tools-Reference.md) ? 46 built-in tools.
- [Troubleshooting](./Troubleshooting.md) ? connectivity, build, PIE, Python.
