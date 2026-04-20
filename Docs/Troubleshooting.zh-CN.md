# UEBridgeMCP ? 故障排查

实战场景下会真正踩到的问题和解法。如果你的现象没有列出，请先收集 `Saved/Logs/<工程名>.log` 里故障时间段的日志 ? 插件的日志分类是 `LogUEBridgeMCP` 和 `LogUEBridgeMCPEditor`。

---

## 1. 连通性

### 1.1 Windows 上客户端报 `ECONNREFUSED`

**原因。** Windows 会优先把 `localhost` 解析成 IPv6（`[::1]`），而插件默认绑在 IPv4 (`127.0.0.1`) 上。

**解法。** 客户端配置一律使用 `127.0.0.1`：

```jsonc
// claude_desktop_config.json
{ "mcpServers": { "unreal-engine": { "url": "http://127.0.0.1:8080/mcp" } } }
```

### 1.2 服务器根本没起来

- 编辑器没开，或插件被禁用。打开 `Edit -> Plugins`，搜 "UEBridgeMCP"，确认 Runtime 和 Editor 两个模块都是启用状态。
- 端口被其他进程占了。Windows 上：
  ```powershell
  netstat -ano | findstr 8080
  ```
  如果 8080 被别的进程占了，要么杀掉它，要么改 `Config/DefaultUEBridgeMCP.ini` 里的 `ServerPort`。

### 1.3 多个 UE 实例之间端口冲突

每个编辑器实例需要自己**独立的**端口。给每个项目分配不同的 `ServerPort`（8080 / 8081 / 8082），客户端 URL 也跟着改。也可以在启动时用 `SOFT_UE_BRIDGE_PORT=8081` 环境变量或 CLI 参数覆盖。

### 1.4 `tools/list` 返回空或 404

- 编辑器模块加载失败，检查启动时的 `LogUEBridgeMCPEditor` 日志。
- `RegisterBuiltInTools` 比依赖模块更早跑了。插件用 `LoadingPhase = PostEngineInit` 是**故意**的，别改。
- 用 `WITH_EDITOR=0` 编的，HTTP 服务只存在于 Editor 模块中。

---

## 2. 构建与模块加载

### 2.1 干净项目上插件编不过

引擎插件依赖没启用。理论上 UE 会根据 `.uplugin` 自动启用，如果没有，手动在 `.uproject` 里加：

- `EditorScriptingUtilities`
- `GameplayAbilities`
- `StateTree` + `GameplayStateTree`
- `PythonScriptPlugin`（在 `Build.cs` 里是硬链接）

然后 Generate Project Files、重新编译。

### 2.2 "UEBridgeMCPEditor.generated.h not found"

重新生成工程文件 ? 右键 `.uproject` -> Generate Visual Studio project files，或运行 `UnrealBuildTool -projectfiles`。

### 2.3 Live Coding 改了代码没生效

- Live Coding 根本没被触发。在编辑器里按 `Ctrl+Alt+F11`，或调 `trigger-live-coding` 工具并设 `wait_for_completion: true`。
- Patch 失败了。查 `Saved/Logs/UnrealEditor.log` 里的 "Patch failed" 消息。常见原因：改了头文件里影响 ABI 的东西（加了 virtual、类尺寸变化、加了 `UPROPERTY`），这种情况必须 full rebuild。

---

## 3. Python 脚本

### 3.1 `run-python-script` 返回 `SyntaxError: expected an indented block`

**根因（已修复，禁止回退）。** `ExecutePython` 会把用户代码包进 `try/except`。原来的缩进实现是 `Command.Replace("\n", "\n    ")`，只对**第一个换行符之后**的行做了缩进 ? 第一行还顶在 `try:` 旁边，导致 `SyntaxError`。

**修复。** 先给第一行加 `    `，**再**做替换：

```cpp
FString IndentedCommand = TEXT("    ") + Command.Replace(TEXT("\n"), TEXT("\n    "));
```

文件：`Plugins/UEBridgeMCP/Source/UEBridgeMCPEditor/Private/Tools/Scripting/RunPythonScriptTool.cpp`。

### 3.2 Python 抛异常后编辑器崩溃

**根因（已修复，stop-gap）。** `ExecPythonCommand` 之后无条件调用 `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)`，在 Python 异常让 GC 处于不一致状态时，会在 `GetTypeHash(FUtf8String) -> CodepointFromUtf8` 里触发 AV 崩溃。

**修复。** 去掉 `ExecutePython()` 里的无脑 GC 调用；只有顶层 `Execute()` 在检测到关卡加载（`bLevelLoadDetected == true`）时才跑 GC。

### 3.3 Python 里 `import unreal` 报错

`PythonScriptPlugin` 没启用。在 `Edit -> Plugins -> Scripting -> Python Editor Script Plugin` 里勾上，重启编辑器。

---

## 4. PIE（Play-In-Editor）

### 4.1 `pie-session start` 或 `pie-session stop` 一直不返回

**根因（已修复）。** 原来的 `ExecuteStart` / `ExecuteStop` 会阻塞游戏线程等待 PIE 就绪（`WaitForPIEReady`、`while + FPlatformProcess::Sleep`）。GameThread 被阻塞时 PIE 就无法 tick，永远也就绪不了 ? 经典死锁，HTTP 请求就一直 hang 住。

**修复。** `RequestPlaySession` / `RequestEndPlayMap` 发起之后立即返回 `"status":"starting"` / `"stopping"`。客户端走轮询：

```json
{"name":"wait-for-world-condition",
 "arguments":{"condition":{"type":"pie_state","equals":"Playing"},
              "timeout_seconds":10}}
```

或者用 `query-gameplay-state` 取一次快照。

文件：`Plugins/UEBridgeMCP/Source/UEBridgeMCPEditor/Private/Tools/PIE/PieSessionTool.cpp`。

### 4.2 `pie-input key:W` 到引擎了但角色不动

**已验证现象**（UE 5.6 + 第三人称模板 + **旧版 `AxisMapping`**）：注入路径 `PlayerController->InputKey` -> `UPlayerInput::InputKey` -> `KeyStateMap` 是通的，**但是**：

- 蓝图的 `InputAxis MoveForward` 节点采样不到这个合成事件。
- `GetInputKeyTimeDown` 和 `GetInputAxisValue` 都返回 `0`。
- Pawn 不动。

**推测根因。** PIE 世界的 tick 顺序和 `InputComponent` 栈与真实输入存在差异，合成出来的按键事件没能走到 AxisMapping 采样器。

**应该怎么办。** 做端到端的角色移动验证时，选下面两条路之一：

1. `pie-input` 走 `event:"move-to"`（AI 寻路）：
   ```json
   {"name":"pie-input",
    "arguments":{"event":"move-to","target_location":[1000,0,0]}}
   ```
2. 通过 `call-function` 直接驱动 Pawn：
   ```json
   {"name":"call-function",
    "arguments":{"target":"ThirdPersonCharacter_C_0",
                 "function":"AddMovementInput",
                 "args":{"WorldDirection":{"X":1,"Y":0,"Z":0},"ScaleValue":1.0}}}
   ```

`pie-input key:X` 留给 UI 按钮和已确认工作的 Enhanced Input 绑定场景使用。

### 4.3 两次 PIE 之间状态没清干净

`assert-world-state` 或 `wait-for-world-condition` 还能看到上一次 PIE 的 Actor。确认你先调了 `pie-session stop` 并轮询到 `Stopped`；编辑器拆 streaming level 有时需要一整个 tick。

---

## 5. 资产与蓝图操作

### 5.1 `create-asset` 报 `UEBMCP_INVALID_PATH`

- 路径必须以 `/Game/` 开头（不是 `Game/`，不是 `Content/`，也不是 Windows 本地路径）。
- 目标目录必须已存在或可被创建。可以先用 `manage-assets` 的 `ensure_directory` action 来建目录。

### 5.2 `edit-blueprint-graph` 返回 `UEBMCP_COMPILE_FAILED`

先用 `"compile": false, "save": false` 调一次拿到干净的 batch 结果，再用 `get-logs` 过滤 `category: LogBlueprint, level: Error` 看编译器报错。常见情况：掉了的输入引脚、父类缺失、变量被改名但其他图还引用着它。

如果错误是结构性的，`auto-fix-blueprint-compile-errors` 通常能自动修：

```json
{"name":"auto-fix-blueprint-compile-errors",
 "arguments":{"blueprint_path":"/Game/BP_Hero","strategies":["reconnect_exec","replace_missing_refs"]}}
```

### 5.3 旧工具名报 "tool not found"

v2 把旧的 v1 工具合并了：

| 旧版（v1） | 新版（v2） |
|---|---|
| `query-blueprint` | `query-blueprint-summary` + `query-blueprint-node` |
| `query-blueprint-graph` | `query-blueprint-graph-summary` + `query-blueprint-node` |
| `query-level` | `query-level-summary` + `query-actor-detail` |
| `query-material` | `query-material-summary` |
| `spawn-actor` / `set-property` / `add-component` | `edit-level-batch` |
| `add-graph-node` / `remove-graph-node` / `connect-graph-pins` / `disconnect-graph-pin` | `edit-blueprint-graph` |
| `set-property`（改蓝图成员时） | `edit-blueprint-members` |
| `analyze-blueprint` | `query-blueprint-summary` |

如果客户端缓存了旧的 schema，重启客户端让它重新拉 `tools/list` 即可。

---

## 6. 迁移与源码管理

### 6.1 把插件挪到另一个项目

UEBridgeMCP **完全**没有游戏逻辑耦合 ? `Source/` 里没有任何 `Lyra*` 或项目专有符号。步骤：

1. 把 `UEBridgeMCP/` 整个拷到 `<新项目>/Plugins/`（不要带 `Binaries/`、`Intermediate/`）。
2. 新项目必须是 C++ 项目，UE 5.6+。
3. 在 `.uproject` 里加 `{"Name":"UEBridgeMCP","Enabled":true}`。
4. 重新生成工程文件、编译。
5. 用 `curl -X POST http://127.0.0.1:8080/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'` 验证 ? 应返回 46 个工具。

### 6.2 通过 Perforce / Git 提交

Blueprint、Material、DataTable 都是二进制。用 `source-control-assets`：

```json
{"name":"source-control-assets",
 "arguments":{"action":"checkout","asset_paths":["/Game/BP_Hero"]}}
```

Git 场景下，插件会跟随编辑器已配置的 Git LFS 设置 ? 插件这边不用额外设置。

---

## 7. 可观测性

### 7.1 提升日志级别

`Config/DefaultUEBridgeMCP.ini`：

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
LogLevel=Verbose
```

重启编辑器。单次请求的详细 trace 会出现在 `LogUEBridgeMCPEditor` 下。

### 7.2 抓所有 MCP 报文

用 `curl -i` 看完整的 JSON-RPC 信封：

```bash
curl -i -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

或者在启动编辑器前设环境变量 `UEBMCP_LOG_SESSIONS=1`，开启内置的 MCP 会话日志。

---

延伸阅读：

- [工具速查手册](./Tools-Reference.zh-CN.md) ? 46 个内置工具。
- [自定义工具开发指南](./ToolDevelopment.zh-CN.md) ? 如何写一个新工具。
- [架构文档](./Architecture.zh-CN.md) ? HTTP / Registry / Session 内部设计。
