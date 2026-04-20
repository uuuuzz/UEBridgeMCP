# UEBridgeMCP ? 工具速查手册（内置 46 个工具）

> 这里列出的每一个工具，都在
> `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp :: RegisterBuiltInTools()` 中出现过一次。
> 如需查看每个工具机读格式的完整 JSON Schema，请直接向运行中的编辑器发起请求：
>
> ```bash
> curl -s -X POST http://127.0.0.1:8080/mcp \
>   -H "Content-Type: application/json" \
>   -H "Accept: application/json,text/event-stream" \
>   -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
> ```
>
> 本文是给人看的速查表；**运行中的编辑器才是权威数据源**。

## 目录

1. [蓝图查询 Blueprint Query（3）](#1-蓝图查询-blueprint-query3)
2. [关卡/世界查询 Level / World Query（3）](#2-关卡世界查询-level--world-query3)
3. [材质查询 Material Query（2）](#3-材质查询-material-query2)
4. [项目/资产/通用查询 Project / Asset / Utility Query（7）](#4-项目资产通用查询-project--asset--utility-query7)
5. [创建/通用写入 Create / Utility Write（3）](#5-创建通用写入-create--utility-write3)
6. [StateTree（5）](#6-statetree5)
7. [脚本 Scripting（1）](#7-脚本-scripting1)
8. [构建 Build（2）](#8-构建-build2)
9. [PIE ? Play-In-Editor（4）](#9-pie--play-in-editor4)
10. [反射调用 Reflection RPC（1）](#10-反射调用-reflection-rpc1)
11. [批量编辑 Batch Edit（6）](#11-批量编辑-batch-edit6)
12. [资产生命周期与校验 Asset Lifecycle & Validation（5）](#12-资产生命周期与校验-asset-lifecycle--validation5)
13. [高阶编排 High-Level Orchestration（4）](#13-高阶编排-high-level-orchestration4)

总计：**46** 个工具（3 + 3 + 2 + 7 + 3 + 5 + 1 + 2 + 4 + 1 + 6 + 5 + 4）。

---

## 约定

- 所有请求都是通过 HTTP POST 到 `http://127.0.0.1:8080/mcp` 的 JSON-RPC 2.0 报文。
- 每次工具调用都使用 MCP 标准的 `tools/call` 方法：
  ```json
  {"jsonrpc":"2.0","id":1,"method":"tools/call",
   "params":{"name":"<工具名>","arguments":{ ... }}}
  ```
- 路径约定：
  - 资产路径使用 UE 的对象路径 ? `/Game/Blueprints/BP_Hero`（不带扩展名）。
  - 关卡路径 ? `/Game/Maps/TestMap`。
- 错误信封：`{"isError": true, "content":[{"type":"text","text":"UEBMCP_<分类>: <消息>"}]}`。
- 成功响应统一携带 `content[0].text`（里面是 JSON 字符串）以及 `isError:false`。

> **关于 v1 与 v2 工具** ? 源码树里还保留着一些旧工具（`query-blueprint`、`query-blueprint-graph`、`query-level`、`query-material`、`set-property`、`add-graph-node`、`spawn-actor` 等），但它们**已不再注册**。其功能已被 v2 批处理工具（`edit-blueprint-graph`、`edit-level-batch`、`edit-material-instance-batch`、`query-*-summary` 系列）吸收。以实际 `tools/list` 返回为准。

---

## 1. 蓝图查询 Blueprint Query（3）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `query-blueprint-summary` | 蓝图摘要：图表/函数/变量/组件的数量统计 | `blueprint_path` |
| `query-blueprint-graph-summary` | 列出蓝图内所有图（事件图/函数图/宏图）及各图节点数 | `blueprint_path` |
| `query-blueprint-node` | 单节点深度信息，包含 object/class 引脚默认值 | `blueprint_path`, `node_guid` |

**示例**
```json
{"name":"query-blueprint-summary",
 "arguments":{"blueprint_path":"/Game/Blueprints/BP_Hero"}}
```

---

## 2. 关卡/世界查询 Level / World Query（3）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `query-level-summary` | 当前关卡的紧凑分桶统计视图 | `world_type`（`editor`\|`pie`） |
| `query-actor-detail` | 某个 Actor 的详细反射信息（组件/属性/标签） | `actor_name` 或 `actor_label` |
| `query-world-summary` | 流式关卡结构、世界构成、玩法设置 | `include[]` |

**示例**
```json
{"name":"query-actor-detail","arguments":{"actor_label":"BP_Hero_C_0"}}
```

---

## 3. 材质查询 Material Query（2）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `query-material-summary` | `Material` 资产概要：着色模型、Domain、混合模式、各类参数数量 | `material_path` |
| `query-material-instance` | 检查 `MaterialInstanceConstant` / `Dynamic` 的覆盖值、父链、标量/向量/贴图参数 | `material_instance_path` |

**示例**
```json
{"name":"query-material-instance",
 "arguments":{"material_instance_path":"/Game/Materials/MI_Hero_Red"}}
```

---

## 4. 项目/资产/通用查询 Project / Asset / Utility Query（7）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `get-project-info` | 项目/引擎/模块/插件/目标平台信息 | *(无参数)* |
| `query-asset` | Content Browser 搜索 + DataTable / DataAsset 检视 | `pattern`, `class_filter`, `path`, `inspect` |
| `get-asset-diff` | 二进制资产（BP/Material/DT）与 SCM 基线的结构化 diff | `asset_path`, `revision` |
| `get-class-hierarchy` | 父类/子类继承树（C++ 与蓝图） | `class_name`, `direction`（`up`\|`down`\|`both`） |
| `find-references` | 查找某资产被谁引用，或它引用了谁 | `asset_path`, `direction` |
| `inspect-widget-blueprint` | UMG Widget 蓝图的控件树、绑定、动画、输入映射 | `widget_path` |
| `get-logs` | 读取 UE 输出日志缓冲区，支持类别/文本/级别过滤 | `category`, `contains`, `limit`, `level` |

**示例**
```json
{"name":"get-logs","arguments":{"category":"LogBlueprint","limit":100,"level":"Warning"}}
```

---

## 5. 创建/通用写入 Create / Utility Write（3）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `create-asset` | 新建 Blueprint / Material / DataTable / Level 等 | `class_name`, `target_path`, `parent_class` |
| `add-widget` | 向 UMG Widget 蓝图的控件树中添加控件 | `widget_path`, `widget_class`, `parent_slot_name` |
| `add-datatable-row` | 为 DataTable 资产新增或更新一行 | `datatable_path`, `row_name`, `fields{}` |

**示例**
```json
{"name":"create-asset",
 "arguments":{"class_name":"Blueprint","target_path":"/Game/Blueprints/BP_NewActor",
              "parent_class":"/Script/Engine.Actor"}}
```

---

## 6. StateTree（5）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `query-statetree` | 查看 StateTree 资产的状态/任务/过渡 | `statetree_path` |
| `add-statetree-state` | 在某父状态下新增一个子状态 | `statetree_path`, `parent_state_id`, `state_name` |
| `remove-statetree-state` | 移除一个状态及其整个子树 | `statetree_path`, `state_id` |
| `add-statetree-transition` | 在两个状态之间增加过渡 | `statetree_path`, `from_state_id`, `to_state_id`, `trigger` |
| `add-statetree-task` | 向某状态添加任务节点 | `statetree_path`, `state_id`, `task_class`, `properties{}` |

**示例**
```json
{"name":"add-statetree-transition",
 "arguments":{"statetree_path":"/Game/AI/ST_Enemy",
              "from_state_id":"Patrol","to_state_id":"Chase",
              "trigger":"OnEnterCondition"}}
```

---

## 7. 脚本 Scripting（1）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `run-python-script` | 在 UE 内嵌 Python 解释器中执行内联代码或 `.py` 文件 | `command` **或** `script_path`, `capture_output` |

会回传 `stdout`、`stderr`，以及 `AsyncTask` 衍生出的日志行。Python 抛出的异常会被捕获并以结构化错误返回，编辑器**不会崩溃**（详见 [Troubleshooting](./Troubleshooting.zh-CN.md)）。

**示例**
```json
{"name":"run-python-script",
 "arguments":{"command":"import unreal\nprint(unreal.SystemLibrary.get_engine_version())"}}
```

---

## 8. 构建 Build（2）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `trigger-live-coding` | 触发 Live Coding 编译（等价于 Ctrl+Alt+F11），可选 `wait_for_completion` | `wait_for_completion`（bool）, `timeout_seconds` |
| `build-and-relaunch` | 关闭**当前**编辑器进程（按 PID 识别），重新构建工程并重启编辑器 | `target_configuration`, `include_editor` |

`build-and-relaunch` 只会关闭连接着 MCP 的这一个编辑器实例，其他打开的编辑器不受影响。仅支持 Windows。

**示例**
```json
{"name":"trigger-live-coding","arguments":{"wait_for_completion":true,"timeout_seconds":60}}
```

---

## 9. PIE ? Play-In-Editor（4）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `pie-session` | **Fire-and-forget** 启动/停止/暂停/恢复 PIE，**绝不阻塞**游戏线程 | `action`（`start`\|`stop`\|`pause`\|`resume`）, `mode` |
| `pie-input` | 向运行中的 PIE 世界注入按键 / 轴输入 / move-to 指令 | `event`（`key`\|`axis`\|`move-to`）, `key`, `axis`, `value`, `target_location` |
| `wait-for-world-condition` | 按 JSON 定义轮询世界条件，支持超时（actor 数量、属性等值……） | `condition{}`, `timeout_seconds`, `poll_interval_ms` |
| `assert-world-state` | 一次性断言，供测试用（返回 pass/fail JSON） | `assertions[]` |

> **已知限制** ? 在 UE 5.6 + 旧版 `AxisMapping` 场景下，注入的 `key:W` / `axis:MoveForward` 可以到达 `UPlayerInput::InputKey`，但蓝图的 `InputAxis` 节点**可能采样不到**。如果目标是端到端驱动角色移动，推荐使用 `event:move-to`（走 AI 寻路），或通过 `call-function` 直接调用 `AddMovementInput`。详见 [Troubleshooting](./Troubleshooting.zh-CN.md)。

**示例**
```json
{"name":"pie-session","arguments":{"action":"start","mode":"SelectedViewport"}}
```

启动之后用 `wait-for-world-condition` 或 `query-gameplay-state` 轮询就绪状态，**不要**循环调用 `pie-session` 自己查询。

---

## 10. 反射调用 Reflection RPC（1）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `call-function` | 调用 Actor / CDO / Subsystem 上任意 `BlueprintCallable` 的 UFUNCTION，带反射参数序列化 | `target`, `function`, `args{}`, `world_type` |

`target` 支持：Actor 名、`/Script/Module.Class`（C++ CDO）、`/Game/...`（蓝图 CDO）、Subsystem 路径。

**示例**
```json
{"name":"call-function",
 "arguments":{"target":"BP_GameMode_C_0","function":"SetDifficulty","args":{"Level":3}}}
```

---

## 11. 批量编辑 Batch Edit（6）

这是 v2 的核心 ? 取代了旧版"单操作"工具。每个批处理工具都支持**事务**（一个 `FScopedTransaction`，支持撤销）、**dry-run 校验**、以及在同一次调用里可选择地进行**编译+保存**。

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `edit-blueprint-graph` | 事务化的图编辑：add / remove / connect / disconnect 节点，支持 alias 交叉引用 | `blueprint_path`, `graph_name`, `operations[]`, `dry_run`, `compile`, `save` |
| `edit-blueprint-members` | 变量/函数/事件派发器的增/删/改名 | `blueprint_path`, `operations[]` |
| `edit-blueprint-components` | SCS 编辑：增/删/改名/挂接/设为根/改默认值 | `blueprint_path`, `operations[]` |
| `edit-level-batch` | 关卡层的批量操作（生成/删除/变换/挂接/分离/复制） | `operations[]`, `world_type` |
| `edit-material-instance-batch` | 一次性修改一个或多个 `MaterialInstanceConstant` 的参数 | `operations[]` |
| `compile-assets` | 编译一个或多个 `Blueprint / WidgetBlueprint / AnimBlueprint` 资产 | `asset_paths[]`, `save_on_success` |

**示例 ? 一次调用创建 BeginPlay → PrintString 连线**
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

## 12. 资产生命周期与校验 Asset Lifecycle & Validation（5）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `manage-assets` | 批量重命名/移动/复制/删除/保存 | `actions[]` |
| `import-assets` | 导入外部文件（FBX / PNG / JPG / TGA / WAV 等） | `files[]`, `destination_path`, `options{}` |
| `source-control-assets` | 源码管理操作：status / checkout / revert / submit / sync | `action`, `asset_paths[]` |
| `capture-viewport` | 截图编辑器视口、PIE 窗口，或指定的编辑器面板 | `source`（`editor`\|`pie`\|`panel`）, `output_path`, `width`, `height` |
| `apply-material` | 按名称/标签/类给 Actor 应用材质或 MIC | `target`, `material_path`, `slot_index` |

**示例**
```json
{"name":"capture-viewport",
 "arguments":{"source":"pie","output_path":"D:/tmp/shot.png","width":1920,"height":1080}}
```

---

## 13. 高阶编排 High-Level Orchestration（4）

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `blueprint-scaffold-from-spec` | 从 JSON 规格（组件/变量/函数/事件图步骤）新建或合并一个蓝图 ? 比 `create-asset` + `edit-blueprint-*` 更高一层 | `target_path`, `spec{}`, `dry_run` |
| `query-gameplay-state` | PIE 玩法快照：PlayerController / Pawn / GameState / GameMode / 世界时间 | `world_type`, `include[]` |
| `auto-fix-blueprint-compile-errors` | 用确定性策略修复蓝图编译错误（重连丢失的引脚、替换缺失引用等） | `blueprint_path`, `strategies[]`, `dry_run` |
| `generate-level-structure` | 按声明式 spec 生成关卡骨架（文件夹/子关卡/World Settings） | `map_path`, `spec{}` |

**示例**
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

## 查询实时清单

机读格式的权威清单，总是以运行中的编辑器为准：

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools | length'
```

预期是 **46**。如果本地看到的数字不是 46，说明你本地的插件副本已过期 ? 请拉取最新 `main` 分支并重新编译。

---

延伸阅读：

- [自定义工具开发指南](./ToolDevelopment.zh-CN.md) ? 如何写一个新工具
- [架构文档](./Architecture.zh-CN.md) ? 模块布局、服务器、注册表、协议
- [故障排查](./Troubleshooting.zh-CN.md) ? IPv6、端口、PIE 死锁、Python GC、pie-input AxisMapping 坑位
