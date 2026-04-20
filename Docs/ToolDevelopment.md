# UEBridgeMCP ? Tool Development Guide

How to author, register, and test a new MCP tool in 5 minutes.

Every built-in tool is a small `UCLASS` subclass of `UMcpToolBase` living under `Source/UEBridgeMCPEditor/{Public,Private}/Tools/...`. There is no reflection magic to fight ? a tool only has to answer four questions:

1. What is my **name** (kebab-case, visible to MCP clients)?
2. What is my **input schema** (what JSON is valid)?
3. Which fields are **required**?
4. What do I **do** when executed?

---

## 1. Header

Create `Source/UEBridgeMCPEditor/Public/Tools/Gameplay/MyEchoTool.h`:

```cpp
// Copyright uuuuzz 2024-2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "MyEchoTool.generated.h"

/**
 * Echoes the `text` argument back - the simplest possible MCP tool.
 * Used as a connectivity sanity check.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UMyEchoTool : public UMcpToolBase
{
    GENERATED_BODY()

public:
    virtual FString GetToolName()        const override { return TEXT("echo"); }
    virtual FString GetToolDescription() const override;
    virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
    virtual TArray<FString> GetRequiredParams() const override { return { TEXT("text") }; }

    virtual FMcpToolResult Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        const FMcpToolContext& Context) override;

    // Optional: classify the tool for observability and routing
    virtual FString GetToolKind()      const override { return TEXT("query"); }
    virtual FString GetResourceScope() const override { return TEXT("generic"); }

    // Optional: `true` means always schedule on the game thread, which is the default
    // for safety. Return `false` if the tool is 100% read-only and thread-safe.
    // virtual bool RequiresGameThread() const override { return false; }
};
```

## 2. Implementation

Create `Source/UEBridgeMCPEditor/Private/Tools/Gameplay/MyEchoTool.cpp`:

```cpp
#include "Tools/Gameplay/MyEchoTool.h"
#include "UEBridgeMCPEditor.h"

FString UMyEchoTool::GetToolDescription() const
{
    return TEXT("Echo the 'text' argument back. Used as a connectivity sanity check.");
}

TMap<FString, FMcpSchemaProperty> UMyEchoTool::GetInputSchema() const
{
    TMap<FString, FMcpSchemaProperty> Schema;
    Schema.Add(TEXT("text"),
        FMcpSchemaProperty::String(TEXT("Text to echo back to the caller.")));
    return Schema;
}

FMcpToolResult UMyEchoTool::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    const FMcpToolContext& /*Context*/)
{
    FString Text;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("text"), Text))
    {
        return FMcpToolResult::Error(TEXT("UEBMCP_MISSING_ARG: 'text' is required"));
    }

    UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("[echo] returning %d chars"), Text.Len());

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("echo"), Text);
    Out->SetNumberField(TEXT("length"), Text.Len());
    return FMcpToolResult::Success(Out);
}
```

## 3. Register

Add one line to `FUEBridgeMCPEditorModule::RegisterBuiltInTools()` in
`Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp`:

```cpp
#include "Tools/Gameplay/MyEchoTool.h"
//...
Registry.RegisterToolClass(UMyEchoTool::StaticClass());
```

That's it. Rebuild (or Live Coding, `Ctrl+Alt+F11`), and the tool is now exposed through `tools/list` and callable via `tools/call`.

## 4. Test

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"echo","arguments":{"text":"hello world"}}}'
```

Expect:

```json
{"result":{"content":[{"type":"text","text":"{\"echo\":\"hello world\",\"length\":11}"}],"isError":false}}
```

---

## Schema helpers

`FMcpSchemaProperty` exposes the common JSON-Schema primitives:

| Helper | Emits | Typical use |
|---|---|---|
| `FMcpSchemaProperty::String(desc)` | `{type:"string"}` | paths, names |
| `FMcpSchemaProperty::Integer(desc)` | `{type:"integer"}` | counts, ids |
| `FMcpSchemaProperty::Number(desc)` | `{type:"number"}` | floats |
| `FMcpSchemaProperty::Boolean(desc)` | `{type:"boolean"}` | flags |
| `FMcpSchemaProperty::Enum(desc, values[])` | `{type:"string",enum:[...]}` | modes |
| `FMcpSchemaProperty::Array(desc, item)` | `{type:"array",items:...}` | lists |
| `FMcpSchemaProperty::Object(desc, properties)` | `{type:"object",properties:...}` | nested |

For complex nested schemas (see `EditBlueprintGraphTool.cpp`), you can also hand-build the `FJsonObject` and override `BuildInputSchemaJson()`.

---

## Golden rules

1. **Game thread safety.** UE object APIs are **not** thread-safe. If your tool's `Execute` might be called on an HTTP worker thread, either:
   - Return `true` from `RequiresGameThread()` (the default); or
   - Use `AsyncTask(ENamedThreads::GameThread, ...)` and a `TPromise<FMcpToolResult>` to marshal work onto the game thread yourself.

2. **Never block the game thread on engine async ops.** That means:
   - PIE start / stop - use fire-and-forget, poll with `wait-for-world-condition`.
   - `LoadPackageAsync`, asset compiles, Live Coding builds - same pattern.
   - A blocked game thread means PIE can't tick, which means the async op you're waiting for can never finish - classic UE deadlock.

3. **Transactions for mutations.**

   ```cpp
   const FScopedTransaction Transaction(LOCTEXT("DoThing", "Do the thing"));
   Target->Modify();
   // ... mutation ...
   ```

   This is what makes Ctrl+Z work for your tool.

4. **Validate up front.** Walk every required field before touching the editor. Fail fast with a structured error:
   ```cpp
   return FMcpToolResult::Error(TEXT("UEBMCP_INVALID_PATH: /Game/... expected"));
   ```

5. **Error codes.** All MCP-facing error messages must start with `UEBMCP_<CATEGORY>:` so automated clients can pattern-match. Common prefixes:
   `UEBMCP_MISSING_ARG`, `UEBMCP_INVALID_ARG`, `UEBMCP_NOT_FOUND`, `UEBMCP_ALREADY_EXISTS`, `UEBMCP_COMPILE_FAILED`, `UEBMCP_TRANSIENT`.

6. **Idempotency.** Writes should re-run cleanly:
   - "create" should be "create-if-missing".
   - "set" should be "set-if-differs".
   - "delete" on a missing target should be a no-op success, not an error.

7. **JSON discipline.** Prefer flat, typed fields over nested opaque strings. For multi-state operations return an envelope:
   ```json
   { "status": "ok", "changed": [...], "unchanged": [...], "warnings": [...] }
   ```

8. **Logging.** Use `LogUEBridgeMCPEditor`:
   - `Verbose` for per-call traces.
   - `Log` for normal activity.
   - `Warning` for recoverable anomalies.
   - `Error` only for actual failures.

9. **Epic C++ style.** PascalCase, `U/A/F/T/E/S/I` prefixes, `TEXT()` for every string literal, `nullptr`, tab indentation, brace-on-new-line. Headers in `Public/` must be self-contained.

---

## Where to put the files

```
Source/UEBridgeMCPEditor/
  Public/Tools/<Category>/<ToolName>Tool.h
  Private/Tools/<Category>/<ToolName>Tool.cpp
```

Existing categories: `Analysis`, `Asset`, `Blueprint`, `Build`, `Debug`, `Level`, `Material`, `PIE`, `Project`, `References`, `Scripting`, `StateTree`, `Widget`, `Write`. Add a new one if your tool doesn't fit.

---

## Validation

Prefer automated coverage when a repo-local harness exists. This repository does not currently ship a committed `Tests/` directory, so choose the lightest validation path that still proves the change.

If your branch adds or already contains `Tests/test_mcp_tools.py`, add a pytest case such as:

```python
def test_echo(mcp_client):
    r = mcp_client.call("echo", {"text": "hello"})
    assert not r["isError"]
    payload = json.loads(r["content"][0]["text"])
    assert payload["echo"] == "hello"
    assert payload["length"] == 5
```

Run it against a live editor:

```bash
cd Plugins/UEBridgeMCP/Tests
python -m pytest test_mcp_tools.py::test_echo -v
```

If there is no repo-local harness yet, document a focused manual validation recipe in your PR or release notes. A minimal example:

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"echo","arguments":{"text":"hello"}}}'
```

---

## Checklist before opening a PR

- [ ] Tool header in `Public/Tools/<Category>/` ? matches class name.
- [ ] Implementation in `Private/Tools/<Category>/` ? matches class name.
- [ ] Registered in `RegisterBuiltInTools()`.
- [ ] `GetToolName()` is unique and kebab-case.
- [ ] Required args validated; errors start with `UEBMCP_`.
- [ ] Mutations wrapped in `FScopedTransaction`.
- [ ] No blocking waits on game-thread async ops.
- [ ] Added to `Tools-Reference.md` **and** `Tools-Reference.zh-CN.md`.
- [ ] Automated coverage added when a harness exists, otherwise manual validation steps documented.
- [ ] `VersionName` in `UEBridgeMCP.uplugin` + `UEBRIDGEMCP_VERSION` in `UEBridgeMCP.h` bumped per semver (see [README](../README.md#versioning)).

---

See also:

- [Architecture](./Architecture.md) ? how the server, registry, and session manager fit together.
- [Tools Reference](./Tools-Reference.md) ? the 46 built-in tools.
- [Troubleshooting](./Troubleshooting.md) ? known pitfalls already fixed, do not regress.
