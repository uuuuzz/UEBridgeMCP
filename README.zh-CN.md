# UEBridgeMCP

**面向 Unreal Engine 5.6+ 的原生 C++ Model Context Protocol (MCP) 插件**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.6%2B-313131?logo=unrealengine)](https://www.unrealengine.com/)
[![Platform](https://img.shields.io/badge/platform-Win64%20%7C%20Mac%20%7C%20Linux-lightgrey.svg)](#)
[![Release](https://img.shields.io/github/v/release/uuuuzz/UEBridgeMCP?include_prereleases&sort=semver)](https://github.com/uuuuzz/UEBridgeMCP/releases)
[![GitHub stars](https://img.shields.io/github/stars/uuuuzz/UEBridgeMCP?style=social)](https://github.com/uuuuzz/UEBridgeMCP/stargazers)
[![GitHub issues](https://img.shields.io/github/issues/uuuuzz/UEBridgeMCP)](https://github.com/uuuuzz/UEBridgeMCP/issues)

**语言：** [English](README.md) | **简体中文**

## 项目概览

UEBridgeMCP 是一个原生 C++ Unreal Engine 插件，通过 Streamable HTTP 将 Unreal Editor 暴露给任意兼容 MCP 的 AI 客户端。MCP 服务直接嵌入编辑器进程内部，因此工具无需额外桥接进程就可以检查和编辑蓝图、关卡、资产、材质、Widget、StateTree、PIE 会话以及构建流程。

**当前版本：** `v1.19.0`

**亮点：**
- **原生 UE 集成** - 使用 Unreal 内置 HTTP 栈与编辑器 API
- **46 个内置工具** - 权威列表位于 `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp`
- **跨客户端兼容** - 支持 Claude Code、Claude Desktop、Cursor、Continue、Windsurf 以及其他 MCP 客户端
- **项目无关** - 不与特定游戏项目耦合，可在 UE 5.6+ C++ 项目之间迁移
- **可扩展** - 可通过继承 `UMcpToolBase` 添加自定义工具

## 文档导航

| 文档 | 说明 |
| --- | --- |
| [工具参考](Docs/Tools-Reference.zh-CN.md) | 内置工具完整清单，按工作流与子系统分组整理 |
| [工具开发](Docs/ToolDevelopment.zh-CN.md) | 如何实现、注册、验证并维护自己的 MCP 工具 |
| [故障排查](Docs/Troubleshooting.zh-CN.md) | 已知故障模式、连接问题、PIE 注意事项与调试步骤 |
| [架构说明](Docs/Architecture.zh-CN.md) | 模块布局、请求生命周期、注册表预热与线程模型 |
| [English Documentation Index](README.md) | 英文首页与对应英文文档入口 |

## 快速开始

### 环境要求

- Unreal Engine **5.6+**
- **C++ Unreal 项目**（纯蓝图项目需要先通过添加任意 C++ 类转换为 C++ 项目）
- `UEBridgeMCP.uplugin` 中启用的核心插件依赖：
  - `EditorScriptingUtilities`
  - `GameplayAbilities`
  - `StateTree`
  - `GameplayStateTree`
- `PythonScriptPlugin` 为 `run-python-script` 提供支持。虽然 `.uplugin` 中将它标记为可选，但当前源码构建会在 `Source/UEBridgeMCPEditor/UEBridgeMCPEditor.Build.cs` 里直接链接它，因此除非你明确移除了 Python 工具链，否则建议保持启用。

### 安装

1. 将插件复制到你的项目中：

```text
<YourProject>/Plugins/UEBridgeMCP/
```

2. 在 `.uproject` 中启用插件：

```json
{
  "Plugins": [
    { "Name": "UEBridgeMCP", "Enabled": true }
  ]
}
```

3. 重新生成项目文件并编译编辑器目标。
4. 启动编辑器并确认模块加载成功。

### 配置

服务端配置位于 `Config/DefaultUEBridgeMCP.ini`：

```ini
[/Script/UEBridgeMCPEditor.McpServerSettings]
ServerPort=8080
bAutoStartServer=true
BindAddress=127.0.0.1
LogLevel=Log
```

**重要：** 在 Windows 上建议优先使用 `127.0.0.1`，不要直接写 `localhost`。部分客户端会将 `localhost` 解析为 IPv6（`[::1]`），而服务端通常绑定在 IPv4 地址上，这会导致本可避免的连接失败。

### 客户端配置

任何支持 HTTP 传输的 MCP 客户端都可以连接，下面给几个常见例子：

**Claude Code CLI**

```bash
claude mcp add --transport http unreal-engine http://127.0.0.1:8080/mcp
```

**Claude Desktop**（`claude_desktop_config.json`）

```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**Cursor**（`.cursor/mcp.json`）

```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

**VS Code Continue**（`.continue/config.json`）

```json
{
  "mcpServers": [
    {
      "name": "unreal-engine",
      "transport": {
        "type": "http",
        "url": "http://127.0.0.1:8080/mcp"
      }
    }
  ]
}
```

### 连通性检查

可以通过 `tools/list` 请求确认服务是否可达：

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

如果插件运行正常，响应中应包含当前编辑器会话已注册的工具集合。

## 内置工具面

权威注册位置为：

```text
Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp
```

在 `v1.19.0` 中，插件共注册了 **46 个内置工具**，可归纳为以下几组：

| 分组 | 数量 | 示例 |
| --- | ---: | --- |
| 查询与检查 | 15 | `query-blueprint-summary`, `query-asset`, `get-asset-diff`, `find-references`, `get-logs` |
| 创建与编辑 | 14 | `create-asset`, `add-widget`, `edit-blueprint-graph`, `edit-level-batch`, `apply-material` |
| StateTree | 5 | `query-statetree`, `add-statetree-state`, `add-statetree-transition` |
| PIE、脚本、构建与 RPC | 8 | `run-python-script`, `trigger-live-coding`, `pie-session`, `pie-input`, `wait-for-world-condition`, `call-function` |
| 高层编排 | 4 | `blueprint-scaffold-from-spec`, `query-gameplay-state`, `auto-fix-blueprint-compile-errors`, `generate-level-structure` |

完整清单与逐工具说明见 [工具参考](Docs/Tools-Reference.zh-CN.md)。

## 架构速览

```text
MCP Client
  -> HTTP POST /mcp
  -> UEBridgeMCPEditor 模块
  -> FMcpServer
  -> FMcpToolRegistry
  -> 继承自 UMcpToolBase 的工具
  -> Unreal Editor 子系统 / 资产 / World / PIE
```

插件包含两个模块：

- `UEBridgeMCP` - 运行时协议类型、Schema 辅助、基类与工具注册表
- `UEBridgeMCPEditor` - 编辑器侧服务、子系统集成、内置工具以及工具栏/状态集成

完整生命周期、预热行为和线程约束见 [架构说明](Docs/Architecture.zh-CN.md)。

## 扩展 UEBridgeMCP

要添加一个新工具：

1. 在 `Source/UEBridgeMCPEditor/Public/Tools/` 和 `Private/Tools/` 下创建新的 `UMcpToolBase` 子类
2. 重写 `GetToolName`、`GetToolDescription`、`GetInputSchema`、`GetRequiredParams` 和 `Execute`
3. 在 `FUEBridgeMCPEditorModule::RegisterBuiltInTools()` 中注册该类
4. 重新编译或触发 Live Coding
5. 如果当前分支或仓库已经有测试基建，就补充自动化验证；如果没有，就至少记录一条可复现的编辑器实机验证路径，比如 `tools/list` 或 `tools/call`

更完整的实现建议与约束见 [工具开发](Docs/ToolDevelopment.zh-CN.md)。

## 版本管理

UEBridgeMCP 对公开发布版本采用语义化版本管理。

- 保持 `UEBridgeMCP.uplugin` 中的 `VersionName` 与 `Source/UEBridgeMCP/Public/UEBridgeMCP.h` 中的 `UEBRIDGEMCP_VERSION` 完全一致。
- 当公开工具名、输入输出 Schema、默认行为或其他对外可见行为发生变化时，在同一个 PR 里同步递增这两个版本号。
- 升级版本时同时更新 `CHANGELOG.md` 与 `RELEASE_NOTES.md`。

## 故障排查入口

如果你遇到以下问题，请先查看 [故障排查](Docs/Troubleshooting.zh-CN.md)：

- 客户端无法连接到 `http://127.0.0.1:8080/mcp`
- `tools/list` 为空，或缺少预期工具
- PIE 启动/停止看起来卡住了
- Python 执行失败或导致编辑器崩溃
- Live Coding 或重新编译后看不到最新代码变更
- 多个 UE 项目占用了同一个服务端口

## 许可证

GNU General Public License v3.0 - 详见 [LICENSE](LICENSE)。

## 相关链接

- [GitHub 仓库](https://github.com/uuuuzz/UEBridgeMCP)
- [更新日志](CHANGELOG.md)
- [Release Notes](RELEASE_NOTES.md)
- [MCP 概览](https://modelcontextprotocol.io)
- [MCP 规范](https://modelcontextprotocol.io/specification)
- [Unreal Engine 文档](https://docs.unrealengine.com/5.6/)

## Star 历史

[![Star History Chart](https://api.star-history.com/svg?repos=uuuuzz/UEBridgeMCP&type=Date)](https://star-history.com/#uuuuzz/UEBridgeMCP&Date)
