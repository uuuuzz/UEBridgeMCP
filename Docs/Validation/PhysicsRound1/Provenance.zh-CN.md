# Physics Round 1 Provenance

目标：

- 记录第一轮 Physics Phase 3A 实现与 smoke 证据。
- 明确边界：本轮不是 GAS、PIE 运行时物理验证、Chaos 深度 diagnostics，也不是破坏性 physics-world 编辑。

实现 provenance：

- Physics 工具是核心编辑器 always-on 工具，直接由 `RegisterBuiltInTools()` 注册。
- 编辑器模块现在依赖 `PhysicsCore`，确保 PhysicalMaterial 与 physics component API 能稳定链接。
- 共享 Physics 行为集中在 `Source/UEBridgeMCPEditor/Private/Tools/Physics/PhysicsToolUtils.*`。
- 工具入口是 `query-physics-summary`、`edit-collision-settings`、`edit-physics-simulation`、`create-physics-constraint`、`edit-physics-constraint`、`apply-physical-material`。
- V1 聚焦安全的 editor-world Actor / Component 编辑，并覆盖 dry-run、rollback、save 与结构化负路径。

验证 provenance：

- 宿主：`G:\UEProjects\MyProject`。
- 资产根目录：`/Game/UEBridgeMCPValidation/PhysicsRound1`。
- 证据根目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\PhysicsRound1`。
- 本实现最终 smoke 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\PhysicsRound1\20260423_195106`。
- 运行时创建的 editor-world Actor 使用带时间戳的 label：`PhysicsRound1_A_20260423_195106` 与 `PhysicsRound1_B_20260423_195106`。
- 最终 smoke 创建了 PhysicalMaterial fixture：`/Game/UEBridgeMCPValidation/PhysicsRound1/PM_PhysicsRound1_20260423_195106`。
- 最终 smoke 请求创建的 constraint component 名称是 `PC_PhysicsRound1_20260423_195106`，后续编辑使用工具返回的实际名称 `PC_PhysicsRound1_20260423_0`，因为 Unreal 对尾部数字 `FName` 做了 compaction。
- 临时 editor-world Actor 已在最终断言后清理。

已知 v1 边界：

- Physics Phase 3A 编辑现有 editor-world Actor 组件；不启动 PIE 物理仿真，也不断言运行时位移。
- Constraint v1 覆盖常用 linear/angular motion、limit、projection、disable-collision 与 break threshold；高级 drive/profile authoring 留给后续。
- `apply-physical-material` 只设置组件 override；批量资产重映射和 material-slot 级策略工具留给后续。
- GAS 与更深层 gameplay runtime 验证会单独规划，不属于本轮 Physics smoke。
