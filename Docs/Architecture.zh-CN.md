# UEBridgeMCP · 架构说明

解释运行时工具面、HTTP 服务、注册表以及会话管理器，如何在一个 UE 编辑器进程内部协同工作。

---

## 整体结构

```text
+---------------------------------+   HTTP POST /mcp   +---------------------------------------+
| MCP 客户端                      | ---JSON-RPC 2.0--> | UE 编辑器进程                        |
|  Claude Desktop / Claude Code   |                    |                                       |
|  Cursor / Windsurf / Continue   | <--- Response ---- |  +- UEBridgeMCP          (Editor)     |
|  Codex / 自定义脚本 / curl      |                    |  |   McpToolBase / Result / Registry  |
+---------------------------------+                    |  |   Schema / Protocol types          |
                                                       |  +- UEBridgeMCPEditor    (Editor)     |
                                                       |      McpServer (FHttpServerModule)    |
                                                       |      McpEditorSubsystem (lifecycle)   |
                                                       |      动态 UMcpToolBase 工具面          |
                                                       |      Toolbar icon + log capture       |
                                                       |  +- UEBridgeMCP* 扩展模块             |
                                                       |      Control Rig / PCG / External AI  |
                                                       +---------------------------------------+
```

- **传输层。** 默认通过 `FHttpServerModule` 绑定 `127.0.0.1:8080`。没有额外外部依赖——不需要 Node.js、不需要 Python 服务，也不需要额外桥接端口。
- **协议。** 使用 MCP 2025-06-18 的 Streamable HTTP 传输，消息封装采用 JSON-RPC 2.0。服务端当前对 POST 请求返回 `application/json`；GET/SSE 流式请求会返回 `405`，因为尚未实现 streaming。
- **线程模型。** HTTP 请求先落到工作线程。凡是会触碰 UE 对象 API 的工具，都会通过 `AsyncTask(ENamedThreads::GameThread, ...)` + `TPromise<FMcpToolResult>` 切回 GameThread 执行。纯只读且线程安全的工具，可以通过从 `RequiresGameThread()` 返回 `false` 选择不切线程。

---

## 模块划分

### `UEBridgeMCP`（Editor）

加载阶段：`Default`。

这个模块承载了服务端和工具实现共同依赖的、尽量与具体项目无关的基础层：

```text
Source/UEBridgeMCP/
  Public/
    UEBridgeMCP.h                   # 版本常量 + 日志分类
    Protocol/
      McpTypes.h                    # FJsonRpcRequest / FJsonRpcResponse / FMcpToolResult
      McpCapabilities.h             # 服务端声明的 capability 结构
    Tools/
      McpToolBase.h                 # 所有工具的 UCLASS 抽象基类
      McpToolRegistry.h             # 线程安全的 ToolName -> class/instance 映射
      McpToolResult.h               # Success / Error / Streaming 工厂
      McpSchemaProperty.h           # JSON-Schema 辅助构造器
```

注册表是一个单例：`FMcpToolRegistry::Get()`。它提供的核心能力包括：

- `RegisterToolClass(UClass*)` —— 按类注册工具，实例按需或预热创建。
- `FindTool(FString Name)` —— 返回已缓存的工具实例（这些实例会通过 `WarmupAllTools()` 预先创建，避免在工作线程上 `NewObject`）。
- `GetToolCount()`、`GetAllTools()` —— 用于响应 `tools/list`。

### `UEBridgeMCPEditor`（Editor）

加载阶段：**`PostEngineInit`**。这是刻意的设计——填充注册表时依赖某些只有引擎初始化之后才可用的编辑器子系统。

```text
Source/UEBridgeMCPEditor/
  Public/
    Server/McpServer.h              # 基于 FHttpServerModule 的轻量封装
    Subsystem/McpEditorSubsystem.h  # UEditorSubsystem：生命周期 + 设置
    UI/McpToolbarExtension.h        # 编辑器工具栏状态图标
    Tools/<Category>/...            # 工具头文件（含已注册与兼容保留项）
  Private/
    UEBridgeMCPEditor.cpp           # RegisterBuiltInTools()：权威工具列表
    Server/McpServer.cpp            # /mcp、/mcp/session/* 路由处理
    Server/McpSessionManager.cpp    # 面向客户端的黏性会话 ID 管理
    Subsystem/McpEditorSubsystem.cpp
    Tools/<Category>/*.cpp          # 工具实现（含已注册与兼容保留项）
```

### 扩展模块（Editor）

加载阶段：**`PostEngineInit`**。

插件描述文件还声明了几个 Editor-only 扩展模块，它们通过同一个 `FMcpToolRegistry` 注册聚焦的工具族：

```text
Source/UEBridgeMCPControlRig/   # edit-control-rig-graph
Source/UEBridgeMCPPCG/          # generate/query/edit/run PCG 工具
Source/UEBridgeMCPExternalAI/   # 外部内容与资产生成工具
```

这些模块把可选工作流工具面留在核心编辑器模块之外；当模块加载成功后，对应工具同样会出现在 `tools/list` 中。

---

## 请求生命周期

```text
客户端：POST /mcp
  body: {"jsonrpc":"2.0","id":42,"method":"tools/call",
         "params":{"name":"edit-blueprint-graph","arguments":{...}}}

        v
[FHttpServerModule 工作线程]
  McpServer::HandleRequest
    -> 解析 JSON-RPC 信封
    -> 会话查找（读取 Mcp-Session-Id 请求头，或创建新会话）
    -> 方法分发：
         initialize   -> FMcpServer::HandleInitialize
         tools/list   -> 遍历 Registry，导出 schema
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
      result = tool->Execute(args, ctx)   // 保持在工作线程执行

        v
[回到工作线程]
  McpServer::WriteResponse
    -> 封装成 {result:{content:[{type:text,text:...}], isError:false}}
    -> 序列化并发送 HTTP 响应
```

### 为什么一定要切回 GameThread

UE 的对象 API（例如 `UObject::Modify`、资产加载、蓝图编译、PIE 控制、SCS 编辑等）**不是**线程安全的。如果在 HTTP 工作线程里直接调用它们，就会和编辑器主循环竞争，轻则返回数据混乱，重则直接把编辑器打崩。

### 为什么要做预实例化

`NewObject<UMcpToolBase>` 本身就要求在 GameThread 上运行。如果等到工作线程里才懒加载工具实例，就又会引入一次切线程。为避免这一点，`FMcpToolRegistry::WarmupAllTools()` 会在 `RegisterBuiltInTools()` 内被调用，而 `RegisterBuiltInTools()` 本身就在 `PostEngineInit` 阶段、由 GameThread 执行，因此可以安全地预创建所有工具实例，并通过 `AddToRoot()` 保持存活。这样一来，工作线程里的 `FindTool` 就可以做到尽量无阻塞。

---

## 会话管理器

MCP 规范允许客户端通过 `Mcp-Session-Id` 请求头维持黏性会话。服务端内部会保存一个小型内存映射：`session_id -> FMcpSession`，用于进度通知和取消执行中的请求。

- 新会话通常在 `initialize` 阶段创建，服务端会在响应头里返回 `Mcp-Session-Id`。
- 后续同一客户端的请求应该尽量带上这个请求头。
- 取消请求通过 `DELETE /mcp/session/<id>` 完成，服务端会终止该会话下正在进行的工作，并释放会话状态。

对于无状态客户端（例如临时执行一次 `curl`），也可以不带这个头；这种情况下每次调用都会得到一个临时会话。

---

## 配置面

`Config/DefaultUEBridgeMCP.ini`：

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
ServerPort=8080            ; int32
bAutoStartServer=true      ; bool
BindAddress=127.0.0.1      ; FString（如需对外暴露可设为 0.0.0.0）
LogLevel=Log               ; Error | Warning | Log | Verbose | VeryVerbose
```

也可以在运行时通过 `UMcpEditorSubsystem::GetSettings()` 覆盖这些设置——这对自动化测试很有用，例如同时拉起多个编辑器实例并让它们监听不同端口。

当前没有接入启动环境变量或 CLI 端口覆盖；如需改端口，请在配置里改 `ServerPort`，或在启动服务前通过编辑器 settings 对象覆盖。

---

## 工具分类信息

除了“名字 / 描述 / 输入 schema / 必填参数”这四个必备问题之外，每个工具还可以额外回答两个分类问题：

```cpp
virtual FString GetToolKind() const;       // "query" | "batch" | "detail" | "mutation"
virtual FString GetResourceScope() const;  // "blueprint" | "blueprint_graph" | "level" | ...
```

这些分类字段本身不会被框架强制校验，但会被两个地方消费：

- `tools/list` 响应——具备更强 UI 的客户端可以按 kind 自动分组显示。
- 可观测性——服务端日志会带上类似 `kind=batch scope=blueprint_graph` 的标签，便于排查某次会话里发生过哪些蓝图图编辑操作。

---

## 错误信封

所有工具错误都会走统一出口：

```cpp
return FMcpToolResult::Error(TEXT("UEBMCP_NOT_FOUND: blueprint /Game/... does not exist"));
```

最终会被编码成：

```json
{"jsonrpc":"2.0","id":42,
 "result":{"content":[{"type":"text","text":"UEBMCP_NOT_FOUND: blueprint /Game/... does not exist"}],
           "isError":true}}
```

遵循 MCP 规范的客户端会把这类结果视作“工具执行失败”（`isError: true`），而不是传输层错误。这一点很重要：AI 客户端可以直接读到错误文本，再自动调整参数后重试，而不必把整次请求视为网络失败。

---

## 服务端生命周期

```text
编辑器启动
   v
UEBridgeMCPEditor 与扩展模块加载（PostEngineInit）
   v
FUEBridgeMCPEditorModule::StartupModule()
   RegisterBuiltInTools()      -> 填充 FMcpToolRegistry
Registry.WarmupAllTools() -> 预实例化已注册的 canonical 工具
   FMcpToolbarExtension::Initialize()
扩展模块在同一个 registry 中注册并预热各自的工具族
   v
FUEBridgeMCPEditorModule 持有 UMcpEditorSubsystem
   v
UMcpEditorSubsystem::Initialize()
   if bAutoStartServer: McpServer::Start(port)
   v
[编辑器正常运行]
   v
编辑器关闭
   v
UMcpEditorSubsystem::Deinitialize()
   McpServer::Stop()
   v
FUEBridgeMCPEditorModule::ShutdownModule()
   FMcpToolbarExtension::Shutdown()
```

`McpServer::Start` 内部使用 `FHttpServerModule::Get().GetHttpRouter(Port)` 获取路由器，并注册以下路由：

- `/mcp`（POST，处理 JSON-RPC 请求）
- `/mcp/session/*`（DELETE，用于取消会话）
- `/health`（GET，用于健康检查，CI 会用到）

---

## 这个插件**不**试图解决什么

- **它不是游戏运行时服务。** HTTP 服务只存在于 Editor 模块里，因此它是编辑器专用能力；打包后的游戏构建不会带上它。
- **它不是权限系统。** 任何能访问 `127.0.0.1:8080` 的客户端都能直接调用全部工具。如果你要把端口暴露到公共网络，请在上游反向代理层自己做鉴权。
- **它不是 Unreal Automation 的替代品。** 自动化测试才是回归验证的正式落地点。这个插件更适合做**探索式操作**：让 AI 代理快速试探编辑器状态、收集问题线索，再把真正稳定的验证沉淀成 `FAutomationSpecBase` 或其他正式测试。

---

延伸阅读：

- [自定义工具开发指南](./ToolDevelopment.zh-CN.md) —— 如何编写一个新工具。
- [工具参考](./Tools-Reference.zh-CN.md) —— 运行时基础工具面与条件工具面总览。
- [故障排查](./Troubleshooting.zh-CN.md) —— 连接、构建、PIE、Python 常见问题。
