# UEBridgeMCP 工具开发指南

教你在 5 分钟内新增、注册并验证一个 MCP 工具。

每个内置工具本质上都是一个很小的 `UCLASS`，继承自 `UMcpToolBase`，放在 `Source/UEBridgeMCPEditor/{Public,Private}/Tools/...` 下。不需要和复杂的反射机制搏斗，一个工具只需要回答四个问题：

1. 我的 **名字** 是什么（kebab-case，MCP 客户端可见）？
2. 我的 **输入 Schema** 是什么（哪些 JSON 合法）？
3. 哪些字段是 **必填** 的？
4. 执行时我到底 **做什么**？

---

## 1. 头文件

创建 `Source/UEBridgeMCPEditor/Public/Tools/Gameplay/MyEchoTool.h`：

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

## 2. 实现

创建 `Source/UEBridgeMCPEditor/Private/Tools/Gameplay/MyEchoTool.cpp`：

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

## 3. 注册

在 `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp` 的
`FUEBridgeMCPEditorModule::RegisterBuiltInTools()` 里加一行：

```cpp
#include "Tools/Gameplay/MyEchoTool.h"
//...
Registry.RegisterToolClass(UMyEchoTool::StaticClass());
```

做到这里就够了。重新编译，或者直接 Live Coding（`Ctrl+Alt+F11`），这个工具就会出现在 `tools/list` 里，也可以被 `tools/call` 调用。

## 4. 调通

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"echo","arguments":{"text":"hello world"}}}'
```

期望返回：

```json
{"result":{"content":[{"type":"text","text":"{\"echo\":\"hello world\",\"length\":11}"}],"isError":false}}
```

---

## Schema 辅助

`FMcpSchemaProperty` 提供了常见 JSON-Schema 基元：

| Helper | Emits | Typical use |
|---|---|---|
| `FMcpSchemaProperty::String(desc)` | `{type:"string"}` | paths, names |
| `FMcpSchemaProperty::Integer(desc)` | `{type:"integer"}` | counts, ids |
| `FMcpSchemaProperty::Number(desc)` | `{type:"number"}` | floats |
| `FMcpSchemaProperty::Boolean(desc)` | `{type:"boolean"}` | flags |
| `FMcpSchemaProperty::Enum(desc, values[])` | `{type:"string",enum:[...]}` | modes |
| `FMcpSchemaProperty::Array(desc, item)` | `{type:"array",items:...}` | lists |
| `FMcpSchemaProperty::Object(desc, properties)` | `{type:"object",properties:...}` | nested |

如果是复杂嵌套 Schema（比如 `EditBlueprintGraphTool.cpp`），也可以手工构造 `FJsonObject`，然后重写 `BuildInputSchemaJson()`。

---

## 黄金规则

1. **游戏线程安全。** UE 对象 API **不是**线程安全的。如果你的工具 `Execute` 可能在 HTTP 工作线程上执行，那么要么：
   - 从 `RequiresGameThread()` 返回 `true`（默认就是这个）；或者
   - 用 `AsyncTask(ENamedThreads::GameThread, ...)` 配合 `TPromise<FMcpToolResult>`，自己把执行切回游戏线程。

2. **不要在游戏线程上阻塞等待引擎异步操作。** 典型场景：
   - PIE 启动/停止：用 fire-and-forget，再通过 `wait-for-world-condition` 轮询。
   - `LoadPackageAsync`、资产编译、Live Coding 构建：同样的思路。
   - 如果你把游戏线程堵住，PIE 就不会 tick；PIE 不 tick，正在等的异步操作也永远完不成，这就是经典 UE 死锁。

3. **修改要放进事务。**

   ```cpp
   const FScopedTransaction Transaction(LOCTEXT("DoThing", "Do the thing"));
   Target->Modify();
   // ... mutation ...
   ```

   这就是为什么用户能对你的工具改动按 `Ctrl+Z`。

4. **先校验，再动编辑器。** 在真正执行前先把所有必填字段走一遍，失败要尽早、结构化地返回：

   ```cpp
   return FMcpToolResult::Error(TEXT("UEBMCP_INVALID_PATH: /Game/... expected"));
   ```

5. **错误码规范。** 所有面向 MCP 的错误消息都必须以 `UEBMCP_<CATEGORY>:` 开头，这样自动化客户端才能做模式匹配。常见前缀包括：
   `UEBMCP_MISSING_ARG`、`UEBMCP_INVALID_ARG`、`UEBMCP_NOT_FOUND`、`UEBMCP_ALREADY_EXISTS`、`UEBMCP_COMPILE_FAILED`、`UEBMCP_TRANSIENT`。

6. **幂等性。** 写操作要能安全重跑：
   - “create” 应当尽量是 “create-if-missing”。
   - “set” 应当尽量是 “set-if-differs”。
   - 删除一个本来就不存在的目标，应该是 no-op success，而不是报错。

7. **JSON 纪律。** 优先返回扁平、强类型字段，不要塞一大串不透明字符串。对于多状态操作，建议返回类似下面的 envelope：

   ```json
   { "status": "ok", "changed": [...], "unchanged": [...], "warnings": [...] }
   ```

8. **日志。** 统一使用 `LogUEBridgeMCPEditor`：
   - `Verbose`：逐调用 trace。
   - `Log`：常规活动。
   - `Warning`：可恢复异常。
   - `Error`：真正失败时再打。

9. **Epic C++ 风格。** PascalCase、`U/A/F/T/E/S/I` 前缀、所有字符串字面量都包 `TEXT()`、使用 `nullptr`、Tab 缩进、大括号换行。放在 `Public/` 下的头文件必须自洽可单独包含。

---

## 文件应该放哪

```text
Source/UEBridgeMCPEditor/
  Public/Tools/<Category>/<ToolName>Tool.h
  Private/Tools/<Category>/<ToolName>Tool.cpp
```

现有分类包括：`Analysis`、`Asset`、`Blueprint`、`Build`、`Debug`、`Level`、`Material`、`PIE`、`Project`、`References`、`Scripting`、`StateTree`、`Widget`、`Write`。如果你的工具实在不适合现有分类，再新增一个。

---

## 验证

优先补自动化覆盖，但前提是仓库里真的有可用的测试基建。当前仓库并没有提交 `Tests/` 目录，所以请选能证明改动正确、同时又和仓库现状一致的验证路径。

如果你的分支已经新增或包含 `Tests/test_mcp_tools.py`，可以补一个 pytest 用例，例如：

```python
def test_echo(mcp_client):
    r = mcp_client.call("echo", {"text": "hello"})
    assert not r["isError"]
    payload = json.loads(r["content"][0]["text"])
    assert payload["echo"] == "hello"
    assert payload["length"] == 5
```

然后针对运行中的编辑器执行：

```bash
cd Plugins/UEBridgeMCP/Tests
python -m pytest test_mcp_tools.py::test_echo -v
```

如果仓库里还没有本地测试 harness，就在 PR 描述或发布说明里写清楚一条聚焦的人工验证路径。最小示例：

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"echo","arguments":{"text":"hello"}}}'
```

---

## 提 PR 前的自检清单

- [ ] 工具头文件位于 `Public/Tools/<Category>/`，文件名与类名一致。
- [ ] 实现文件位于 `Private/Tools/<Category>/`，文件名与类名一致。
- [ ] 已在 `RegisterBuiltInTools()` 中注册。
- [ ] `GetToolName()` 唯一且符合 kebab-case。
- [ ] 必填参数校验到位，错误消息以 `UEBMCP_` 开头。
- [ ] 所有修改操作都包在 `FScopedTransaction` 里。
- [ ] 没有在 GameThread 上阻塞等待异步操作。
- [ ] 已同步更新 `Tools-Reference.md` **和** `Tools-Reference.zh-CN.md`。
- [ ] 若存在测试 harness，则补充自动化覆盖；否则写明人工验证步骤。
- [ ] `UEBridgeMCP.uplugin` 里的 `VersionName` 和 `Source/UEBridgeMCP/Public/UEBridgeMCP.h` 里的 `UEBRIDGEMCP_VERSION` 已按 semver 同步递增（参考 [README](../README.zh-CN.md#版本管理)）。

---

延伸阅读：

- [架构说明](./Architecture.zh-CN.md) - 服务、注册表、会话管理如何协作。
- [工具参考](./Tools-Reference.zh-CN.md) - 46 个内置工具总览。
- [故障排查](./Troubleshooting.zh-CN.md) - 已修复的坑位，不要回归。
