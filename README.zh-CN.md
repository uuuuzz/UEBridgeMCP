# UEBridgeMCP

**面向 Unreal Engine 5.6+ 的原生 C++ Model Context Protocol (MCP) 插件**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.6%2B-313131?logo=unrealengine)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/platform-Win64%20%7C%20Mac%20%7C%20Linux-lightgrey.svg)](#)
[![Release](https://img.shields.io/github/v/release/uuuuzz/UEBridgeMCP?include_prereleases&sort=semver)](https://github.com/uuuuzz/UEBridgeMCP/releases)
[![GitHub stars](https://img.shields.io/github/stars/uuuuzz/UEBridgeMCP?style=social)](https://github.com/uuuuzz/UEBridgeMCP/stargazers)
[![GitHub issues](https://img.shields.io/github/issues/uuuuzz/UEBridgeMCP)](https://github.com/uuuuzz/UEBridgeMCP/issues)

📖 **语言 / Language**：[English](README.md) | **简体中文**

## 目录

- [项目简介](#项目简介)
- [快速开始](#快速开始)
  - [安装](#安装)
  - [配置](#配置)
  - [客户端配置](#客户端配置)
- [工具一览](#工具一览)
  - [v2 核心工作流](#v2-核心工作流)
  - [v2 查询 / 详情类工具](#v2-查询--详情类工具)
  - [v2 批量 / 断言类工具](#v2-批量--断言类工具)
  - [结构化响应约定](#结构化响应约定)
  - [Python 脚本](#python-脚本v1100-新增)
  - [蓝图分析](#蓝图分析7-个工具)
  - [关卡 / 世界类工具](#关卡--世界类工具2-个)
  - [项目配置](#项目配置1-个工具)
  - [分析类工具](#分析类工具2-个)
  - [资源管理](#资源管理1-个工具)
- [架构](#架构)
- [二次开发](#二次开发)
- [许可协议](#许可协议)
- [参与贡献](#参与贡献)
- [相关链接](#相关链接)

## 项目简介

ue-bridge-mcp 是一款原生 C++ 的 Unreal Engine 插件，基于 Streamable HTTP 实现了 [Model Context Protocol (MCP)](https://modelcontextprotocol.io) 协议。它让 Claude、Cursor、Windsurf 等 AI 助手能够直接与 Unreal Editor 进行交互。

**核心特性：**
- ✅ **零外部依赖** —— 基于 UE 内置的 `FHttpServerModule` 实现
- ✅ **跨 LLM 兼容** —— 支持 Claude、Cursor、Windsurf、VS Code Copilot、Continue、OpenAI 等
- ✅ **编辑器原生集成** —— HTTP 服务直接运行在 UE Editor 内，无需外挂进程
- ✅ **易于扩展** —— 提供简洁的工具注册系统，方便接入自定义 MCP 工具

## 快速开始

### 安装

1. 将插件克隆到项目的 `Plugins` 目录：
```bash
cd YourProject/Plugins
git clone https://github.com/uuuuzz/UEBridgeMCP.git
```

2. 重新生成项目文件并编译项目

3. 在 `.uproject` 中或通过 `Editor → Plugins` 启用该插件

### 配置

插件会在 `127.0.0.1:8080/mcp` 上启动一个 HTTP 服务，相关配置位于 `Config/DefaultUEBridgeMCP.ini`：

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
; HTTP 服务端口（默认：8080）
; 如果同时打开多个 UE 实例，请为每个实例配置不同端口
ServerPort=8080

; 是否在编辑器启动时自动启动服务
bAutoStartServer=true

; 监听地址（默认：127.0.0.1，仅本机可访问，较为安全）
BindAddress=127.0.0.1
```

> **提示：** 同时运行多个 UE 项目时，请为每个项目配置不同的 `ServerPort`（例如 8080、8081、8082）。

### 客户端配置

**Claude Code CLI**：
```bash
claude mcp add --transport http unreal-engine http://127.0.0.1:8080/mcp
```

**Claude Desktop**（`claude_desktop_config.json`）：
```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**Cursor**（`.cursor/mcp.json`）：
```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**VS Code Continue**（`.continue/config.json`）：
```json
{
  "mcpServers": [{
    "name": "unreal-engine",
    "transport": { "type": "http", "url": "http://127.0.0.1:8080/mcp" }
  }]
}
```

## 工具一览

当前插件已内置 **v2 面向 Agent 的工具体系**，围绕以下四类能力构建：

- **概要查询（summary queries）** —— 快速发现、响应体积小
- **详情查询（detail queries）** —— 精准进一步排查
- **批量变更（batch mutation tools）** —— 事务式编辑
- **运行时断言（runtime assertions）** —— 用于 PIE 中的验证

### v2 核心工作流

推荐的 Agent 工作流是：

`概要查询 → 定向详情 → 事务式编辑 → 编译/保存 → 断言`

这一流程取代了旧版依赖大量"微操工具"链式调用的使用模式，例如：

- `add-graph-node`
- `connect-graph-pins`
- `disconnect-graph-pin`
- `remove-graph-node`
- `set-property`

### v2 查询 / 详情类工具

- `query-blueprint-summary`
- `query-blueprint-graph-summary`
- `query-blueprint-node`
- `query-world-summary`
- `query-level-summary`
- `query-actor-detail`
- `query-material-summary`
- `query-material-instance`

### v2 批量 / 断言类工具

- `edit-blueprint-graph`
- `edit-blueprint-members`
- `edit-blueprint-components`
- `edit-level-batch`
- `edit-material-instance-batch`
- `assert-world-state`

### 结构化响应约定

所有 v2 工具均按 **MCP-first** 规范返回统一的结果信封：

- `content`
- `structuredContent`
- `isError`（仅在调用失败时出现）
- `_meta.diagnostics`
- `_meta.timing`
- `_meta.stats`

其中供人类阅读的 `content.text` 刻意保持为简短摘要，不会重复完整数据。机器消费方应优先读取 `structuredContent`。

### Actor Handle 约定

Level/World 概要类工具返回的 actor handle 使用以下稳定身份字段：

- `entity_id`：actor 的对象路径
- `resource_path`：所在 world 的路径
- `display_name`：仅用于展示的人类可读名称
- `session_id`：创建该 handle 时所使用的 MCP 会话

`query-actor-detail` 解析 handle 时会优先使用 `resource_path + entity_id`，仅在 handle 信息不完整时才回退到按 actor 名匹配。

### 旧版工具

源码中仍保留了部分旧版工具作为内部实现辅助或兼容层，但上文提到的 v2 工具才是未来对外的主要接口。

### Python 脚本（v1.10.0 新增）

#### `run-python-script`
在 Unreal Editor 的 Python 环境中执行 Python 脚本。

**参数：**
- `script`（string，可选）—— 内联 Python 代码
- `script_path`（string，可选）—— Python 脚本文件路径
- `arguments`（object，可选）—— 可通过 `unreal.get_mcp_args()` 访问的参数

**前置条件：** 需要启用 `PythonScriptPlugin`

**示例：**
```json
{
  "script": "import unreal\nprint(unreal.SystemLibrary.get_project_name())",
  "arguments": {"asset_path": "/Game/MyAsset"}
}
```

---

### 蓝图分析（7 个工具）

#### `analyze-blueprint`
完整的蓝图结构分析，包括父类、函数、变量和组件。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径（例如 `/Game/Blueprints/BP_Character`）

**返回值：** 包含完整蓝图元数据的 JSON

---

#### `get-blueprint-functions`
列出所有函数的签名、参数、返回类型及元数据。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径
- `function_filter`（string，可选）—— 按函数名过滤（支持通配符）

**返回值：** 带完整签名的函数定义数组

---

#### `get-blueprint-variables`
列出所有变量的类型、默认值、网络复制设置及元数据。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径
- `variable_filter`（string，可选）—— 按变量名过滤（支持通配符）

**返回值：** 带类型和默认值的变量数组

---

#### `get-blueprint-components`
获取组件层级结构，包含 transform 和附加关系。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径
- `include_transforms`（boolean，可选）—— 是否包含组件 transform（默认 true）

**返回值：** 带层级关系和 transform 的组件树

---

#### `get-blueprint-graph`
读取完整的蓝图图表结构，包含所有节点、连接和 pin 数据。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径
- `graph_name`（string，可选）—— 指定图表名
- `graph_type`（string，可选）—— 过滤条件：`event`、`function` 或 `macro`
- `include_positions`（boolean，可选）—— 是否包含节点的 X/Y 坐标（默认 false）

**返回值：** 所有图表及其节点、pin 和连接

---

#### `get-blueprint-node`
通过 GUID 获取指定蓝图节点的详细信息。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径
- `node_guid`（string，必填）—— 要检查的节点 GUID

**返回值：** 节点的完整详情，包含 pin 和连接

---

#### `get-blueprint-defaults`
读取蓝图 CDO（Class Default Object）的属性默认值。

**参数：**
- `asset_path`（string，必填）—— 蓝图资源路径
- `property_filter`（string，可选）—— 按属性名过滤（支持通配符）
- `category_filter`（string，可选）—— 按属性分类过滤

**返回值：** 所有属性默认值，包含类型、分类与标志位

---

### 关卡 / 世界类工具（2 个）

#### `query-level`
列出当前关卡中的 Actor，支持多种过滤条件。

**参数：**
- `class_filter`（string，可选）—— 按 Actor 类过滤（支持通配符）
- `folder_filter`（string，可选）—— 按 World Outliner 文件夹路径过滤
- `tag_filter`（string，可选）—— 按 Actor 标签过滤
- `include_hidden`（boolean，可选）—— 是否包含隐藏 Actor（默认 false）
- `include_components`（boolean，可选）—— 是否返回组件列表（默认 false）
- `include_transform`（boolean，可选）—— 是否返回 transform（默认 true）
- `limit`（integer，可选）—— 返回的最大条数（默认 100）

**返回值：** Actor 数组，按需附带 transform 与组件信息

---

#### `get-actor-details`
对关卡中指定 Actor 进行深度检查。

**参数：**
- `actor_name`（string，必填）—— 要检查的 Actor 名或 label
- `include_properties`（boolean，可选）—— 是否返回所有属性（默认 true）
- `include_components`（boolean，可选）—— 是否返回组件详情（默认 true）

**返回值：** 完整的 Actor 详情，包含属性和组件层级

---

### 项目配置（1 个工具）

#### `get-project-settings`
查询项目配置。

**参数：**
- `section`（string，可选）—— 查询区块：`input`、`collision`、`tags`、`maps` 或 `all`（默认 `all`）

**返回值：** 对应配置区块的 JSON，包括：
- **input**：Action/Axis 映射（含按键与修饰键）
- **collision**：碰撞 Profile、通道及响应设置
- **tags**：Gameplay Tag 来源与设置
- **maps**：默认地图与 GameMode

---

### 分析类工具（2 个）

#### `get-class-hierarchy`
浏览类继承树，显示父类与子类。

**参数：**
- `class_name`（string，必填）—— 要检查的类（例如 `AActor`、`UActorComponent`）
- `direction`（string，可选）—— `parents`、`children` 或 `both`（默认 `both`）
- `include_blueprints`（boolean，可选）—— 是否包含蓝图子类（默认 true）
- `depth`（integer，可选）—— 最大继承深度（默认 10）

**返回值：** 包含父链与子类的类继承结构

---

#### `inspect-data-asset`
读取 DataTable / DataAsset 的内容。

**参数：**
- `asset_path`（string，必填）—— DataTable 或 DataAsset 路径
- `row_filter`（string，可选）—— 按行名过滤（支持通配符，仅 DataTable 生效）

**返回值：**
- **DataTable**：所有行及其字段数据
- **DataAsset**：所有属性及对应值

---

### 资源管理（1 个工具）

#### `search-assets`
按名称、类或路径搜索资源，支持通配符。

**参数：**
- `pattern`（string，可选）—— 搜索匹配模式（支持通配符）
- `class_filter`（string，可选）—— 按资源类过滤
- `path_filter`（string，可选）—— 按资源路径过滤
- `limit`（integer，可选）—— 返回的最大条数（默认 100）

**返回值：** 匹配到的资源数组，包含路径与类型

## 架构

```
┌──────────────────┐  HTTP POST   ┌──────────────────────────────┐
│  Claude / Cursor │ ──────────►  │  UE 编辑器                   │
│  Windsurf / 其他 │  JSON-RPC    │  └─ UEBridgeMCP 插件         │
│                  │ ◄──────────  │     └─ FHttpServerModule     │
│                  │   响应       │        (127.0.0.1:8080/mcp)  │
└──────────────────┘              └──────────────────────────────┘
```

**模块划分：**
- `UEBridgeMCP`（Runtime）—— MCP 协议核心层（JSON-RPC、工具注册表）
- `UEBridgeMCPEditor`（Editor）—— HTTP 服务 + UE 编辑器侧工具实现

## 二次开发

### 添加自定义工具

通过继承 `UMcpToolBase`（声明位于 `Source/UEBridgeMCP/Public/McpToolBase.h`）
即可注册自定义工具。需要重写 `GetToolName`、`GetToolDescription`、
`GetInputSchema` 以及 `Execute` 方法，并在模块启动时通过
`FMcpToolRegistry::Get().RegisterTool<YourTool>()` 完成注册。

`Source/UEBridgeMCPEditor/Private/Tools/` 目录下的内置工具可作为参考实现。

### 编译

需要 Unreal Engine 5.6 或更高版本。

```bash
# 使用 UnrealBuildTool 进行编译
<UE>/Engine/Build/BatchFiles/Build.bat YourProjectEditor Win64 Development
```

## 许可协议

GNU General Public License v3.0 —— 详见 [LICENSE](LICENSE)

## 参与贡献

欢迎任何形式的贡献！请通过 Issue 或 Pull Request 提交你的反馈和代码。

## 相关链接

- [GitHub 仓库](https://github.com/uuuuzz/UEBridgeMCP)
- [更新日志](CHANGELOG.md)
- [Model Context Protocol 官网](https://modelcontextprotocol.io)
- [MCP 协议规范](https://modelcontextprotocol.io/specification)

## Star 历史

[![Star History Chart](https://api.star-history.com/svg?repos=uuuuzz/UEBridgeMCP&type=Date)](https://star-history.com/#uuuuzz/UEBridgeMCP&Date)
