# Blueprint Round 2 Smoke Checklist

## 目标

在 `G:\UEProjects\MyProject` 中验证 Blueprint Phase 1C：编译分析、结构性 fixup、interface conform，以及精选 Actor pattern。

## 宿主与资产范围

- 宿主项目：`G:\UEProjects\MyProject`
- MCP endpoint：`http://127.0.0.1:8080/mcp`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/BlueprintRound2`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\<timestamp>`
- 本轮不新增专门自动化 harness。

## 协议与可见性

- [x] `initialize` 成功。
- [x] `tools/list` 成功。
- [x] `tools/list` 包含 `analyze-blueprint-compile-results`。
- [x] `tools/list` 包含 `apply-blueprint-fixups`。
- [x] `tools/list` 包含 `create-blueprint-pattern`。
- [x] `tools/list` 仍包含 `compile-assets`、`auto-fix-blueprint-compile-errors`、`blueprint-scaffold-from-spec`。

## Analyze 验证

- [x] 干净 Actor Blueprint 返回 `compile.success=true`。
- [x] 干净 Actor Blueprint 返回 `issues=[]`。
- [x] 运行时制造的 broken Blueprint 返回至少一条 compile diagnostic。
- [x] 运行时制造的 broken Blueprint 返回结构性 finding。
- [x] `suggested_fixups` 符合 v1 对 unresolved / broken reference 的映射。

## Fixup 验证

- [x] `dry_run=true` 成功且不修改资产。
- [x] `refresh_all_nodes` 后执行 `remove_orphan_pins` 能移除 orphan pins。
- [x] `conform_implemented_interfaces` 能修复缺失的 interface graph。
- [x] invalid action 返回结构化失败。
- [x] `rollback_on_error=true` 在 invalid-action 路径下不会留下成功的半成品 action。

## Interface Conformance Fixture

- [x] 通过 `create-asset(asset_class="BlueprintInterface")` 运行时创建 Blueprint Interface。
- [x] 运行时创建 Actor Blueprint target。
- [x] 先用 `sync_graphs=false` 添加空 interface。
- [x] 在 target 已实现 interface 后，再给 BPI 添加 non-void interface function。
- [x] 确认 `query-blueprint-findings` 报告 `missing_interface_graph`。
- [x] 执行 `conform_implemented_interfaces`。
- [x] 确认 finding 消失且 target Blueprint 可编译。

## Pattern 验证

- [x] `create-blueprint-pattern(pattern="logic_actor_skeleton")` 能创建并编译 Actor Blueprint。
- [x] `create-blueprint-pattern(pattern="toggle_state_actor")` 能创建并编译 Actor Blueprint。
- [x] `create-blueprint-pattern(pattern="interaction_stub_actor")` 能创建并编译 Actor Blueprint。
- [x] Pattern 图完成 layout，并包含 comment region。
- [x] 复用已有 pattern 资产路径会返回 `UEBMCP_ASSET_ALREADY_EXISTS`。

## 回归验证

- [x] `compile-assets` 仍可编译 Phase 1C pattern 资产，并保持 schema 不变地返回 diagnostics。
- [x] `auto-fix-blueprint-compile-errors` 仍接受原有四个 strategy 的 `dry_run`。
- [x] `blueprint-scaffold-from-spec` 仍注册，但不作为 curated pattern 的底层实现。

## 已接受证据

- 最新接受证据：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254`
- 汇总文件：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254\summary.json`
