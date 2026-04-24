# 发布前预检

发布打 tag 或打包前运行这套检查。它验证运行时工具清单和 UnrealMCPServer 兼容层，不依赖写死的工具数量。

## 运行方式

先启动启用了 UEBridgeMCP 的 Unreal Editor，然后在插件根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File Validation\Smoke\Invoke-ReleasePreflight.ps1
```

如果编辑器没有运行，只想做静态检查：

```powershell
powershell -ExecutionPolicy Bypass -File Validation\Smoke\Invoke-ReleasePreflight.ps1 -AllowOffline
```

脚本会把证据写到：

```text
Tmp/Validation/ReleasePreflight/<timestamp>/summary.json
```

## 验收门槛

发布前应满足：

- 静态兼容 alias 检查通过。
- `initialize.capabilities.tools.registeredCount` 等于 `tools/list.length`。
- `resources/list` 和 `prompts/list` 成功返回。
- 预期的 UnrealMCPServer 风格 alias 能在 `tools/list` 里看到。
- 只读或 dry-run alias 抽样调用成功。
- 安全探针确认高风险操作会拒绝未确认的真实执行。

## 安全预期

高风险工具现在需要显式信号才会修改编辑器状态：

- `build-and-relaunch`：用 `dry_run=true` 做计划预览；真实执行需要 `confirm_shutdown=true`。
- `run-python-script`：用 `dry_run=true` 做计划预览；真实执行需要 `confirm_execution=true`。
- `manage-assets`：非 dry-run 的 `delete` 需要 `confirm_delete=true`；`consolidate` 需要 `confirm_consolidate=true`。
- `source-control-assets`：支持根级 `dry_run`；非 dry-run 的 `revert`、`submit`、`sync` 需要 `confirm_write=true`。

这些确认项刻意保持为简单布尔值，方便 MCP 客户端只在助手明确说明破坏性意图后再设置。
