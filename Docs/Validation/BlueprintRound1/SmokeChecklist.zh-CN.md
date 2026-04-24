# Blueprint 第一轮 Smoke Checklist

## 目标

在 `G:\UEProjects\MyProject` 中完成 Blueprint Phase 1A + 1B 的宿主工程验证，不新建独立 smoke 工程，也不为这一轮新增自动化 harness。

## 宿主与资产范围

- 宿主工程：`G:\UEProjects\MyProject`
- MCP 端点：`http://127.0.0.1:8080/mcp`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/BlueprintRound1`
- 运行时创建的 Blueprint 资产模式：`/Game/UEBridgeMCPValidation/BlueprintRound1/BP_BlueprintRound1_<timestamp>`
- 运行时创建的 Blueprint Interface 资产模式：`/Game/UEBridgeMCPValidation/BlueprintRound1/BPI_BlueprintRound1_<timestamp>`
- 可选证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound1\<timestamp>`

## 前置条件

- `MyProject` 编辑器已启动，且 `UEBridgeMCP` 自动启动服务器。
- `initialize.capabilities.tools.registeredCount` 和 `tools/list` 可读。
- 本轮接受手工 MCP 请求/响应记录，不要求额外的专用验证 harness。

## 协议与可见性

- [ ] `initialize` 可以成功连接 `http://127.0.0.1:8080/mcp`
- [ ] `tools/list` 成功
- [ ] `tools/list` 中能看到：
  - [ ] `create-blueprint-function`
  - [ ] `create-blueprint-event`
  - [ ] `edit-blueprint-function-signature`
  - [ ] `manage-blueprint-interfaces`
  - [ ] `layout-blueprint-graph`
  - [ ] `query-blueprint-findings`
- [ ] `tools/list` 中仍保留 `edit-blueprint-members` 和 `edit-blueprint-graph`

## 资产准备

- [ ] 通过 `create-asset(asset_class="BlueprintInterface")` 创建 `/Game/UEBridgeMCPValidation/BlueprintRound1/BPI_BlueprintRound1_<timestamp>`
- [ ] 通过 `create-asset(asset_class="Blueprint")` 创建 `/Game/UEBridgeMCPValidation/BlueprintRound1/BP_BlueprintRound1_<timestamp>`
- [ ] 在执行 `add_interface` 之前，先给运行时创建的 BPI 增加至少一个 interface function；为了稳定验证 `sync_graphs`，这个函数应使用非 void 签名，例如带一个 bool output
- [ ] 父类为 `/Script/Engine.Actor`
- [ ] Round 1 变更开始前，资产本身可编译、可保存

## 成员层验证

- [ ] `edit-blueprint-members` 至少创建一个 Blueprint variable
- [ ] `edit-blueprint-members` 至少创建一个 function
- [ ] `edit-blueprint-members` 至少创建一个 event dispatcher
- [ ] `create-blueprint-function` 成功创建带 inputs / outputs 的函数图
- [ ] `edit-blueprint-function-signature` 能更新 category、tooltip、metadata 或 pin 默认值，且这些值可以稳定回读
- [ ] 在某个函数内走通 local variable 动作：
  - [ ] `create_local_variable`
  - [ ] `rename_local_variable`
  - [ ] `set_local_variable_properties`
  - [ ] `delete_local_variable`
- [ ] 至少验证一次 `dry_run=true` 的成员编辑成功且不落盘
- [ ] 至少验证一次带 `rollback_on_error=true` 的结构化失败路径

## Interface 验证

- [ ] 通过 `manage-blueprint-interfaces` 或 `edit-blueprint-members` 使用 `interface_path` 增加运行时创建的 interface
- [ ] add-interface 的结果里包含 `implemented_interfaces`
- [ ] add-interface 的结果里包含 `touched_graphs`
- [ ] add-interface 路径不显式传 `sync_graphs`，用来验证默认行为
- [ ] remove-interface 路径被实际执行过
- [ ] 确认 `sync_graphs=true` 的默认行为
- [ ] 至少留存一条 `interface_path` 缺失或不是 interface 的结构化失败路径

## 图层验证

- [ ] `create-blueprint-event` 在 `EventGraph` 中创建自定义事件
- [ ] `edit-blueprint-graph` 至少成功覆盖这些 Blueprint 专用 op：
  - [ ] `add_branch`
  - [ ] `add_sequence`
  - [ ] `add_call_function`
  - [ ] `add_get_variable`
  - [ ] `add_set_variable`
  - [ ] `add_reroute`
  - [ ] `add_comment`
  - [ ] `comment_region`
  - [ ] `move_nodes_batch`
  - [ ] `layout_graph`
- [ ] `auto_connect` 至少对一组节点成功
- [ ] 图编辑后的 Blueprint 仍然可以编译、保存

## Findings 验证

- [ ] 在验证 Blueprint 上故意制造至少一个结构性问题
- [ ] `query-blueprint-findings` 能报出一个或多个预期 code：
  - [ ] `orphan_pin`
  - [ ] `broken_or_deprecated_reference`
  - [ ] `unlinked_required_pin`
  - [ ] `missing_default_value`
  - [ ] `unresolved_member_reference`
  - [ ] `missing_interface_graph`
- [ ] 修复该问题
- [ ] 再次执行 `query-blueprint-findings`，确认对应 finding 消失

## 编译、保存与验收出口

- [ ] 通过 `compile-assets` 或工具内 compile 路径完成最终编译
- [ ] 最终资产保存成功
- [ ] 最终 Blueprint 在 `MyProject` 中可重新加载
- [ ] 本轮没有为了验证额外引入新的自动化 harness

## 建议留存的证据

- 一份成功的 `initialize` 响应片段
- 一份包含 6 个新 Blueprint 工具的 `tools/list` 片段
- 验证 Blueprint 与 Blueprint Interface 资产路径，以及最终 compile/save 结果
- 一组 `query-blueprint-findings` 修复前后的对照
- 一组正向 add/remove-interface 请求响应，以及一条负路径请求响应
