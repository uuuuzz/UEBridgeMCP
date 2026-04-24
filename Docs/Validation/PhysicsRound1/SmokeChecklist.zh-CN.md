# Physics Round 1 Smoke Checklist

范围：

- 只覆盖 Physics Phase 3A。
- 不验证 GAS、Chaos 运行时仿真断言、破坏性 physics-world 修改或 PIE gameplay 行为。
- 宿主项目固定为 `G:\UEProjects\MyProject`。
- 验证资产固定放在 `/Game/UEBridgeMCPValidation/PhysicsRound1`。
- 证据目录固定放在 `G:\UEProjects\UEBridgeMCP\Tmp\Validation\PhysicsRound1\<timestamp>`。

Checklist：

- `tools/list` 能看到 6 个 Physics Phase 3A 工具：`query-physics-summary`、`edit-collision-settings`、`edit-physics-simulation`、`create-physics-constraint`、`edit-physics-constraint`、`apply-physical-material`。
- `query-physics-summary` 能返回有效 world 摘要。
- 运行时创建的 editor-world Actor 可以用 actor scope 查询，并返回 PrimitiveComponent 的 collision / simulation 明细。
- `edit-collision-settings` 覆盖 `dry_run=true`、正向 collision profile / enabled mode / object channel / response 修改、rollback 和 save 路径。
- `edit-physics-simulation` 覆盖 `dry_run=true`、正向 simulate-physics、gravity、mass、mass scale、damping、wake/sleep、rollback 和 save 路径。
- `apply-physical-material` 覆盖 `dry_run=true`，以及使用运行时创建的 PhysicalMaterial 资产执行正向 override。
- `create-physics-constraint` 覆盖 `dry_run=true`，以及在两个 Actor PrimitiveComponent 之间正向创建 constraint。
- `edit-physics-constraint` 覆盖正向 constraint 编辑，包括 disable-collision、projection、linear limits、angular limits 和 break thresholds。
- 至少一条缺失 component 路径返回结构化错误。
- 对缺失 Actor 执行 actor-specific `query-physics-summary` 时，应返回结构化 `UEBMCP_ACTOR_NOT_FOUND`，而不是静默回退到 world scope。
- smoke 过程中创建的临时 editor-world Actor 在最终断言后需要清理。

证据要求：

- 保留 tool visibility、project info、fixture 创建、world query、actor query、dry-run/apply、constraint create/edit、负路径、最终 actor summary 和 cleanup 的原始 MCP 响应。
- 保留一个紧凑的 `summary.json`，记录目标 Actor 名称、PhysicalMaterial 路径、请求与实际 constraint component 名称、成功标记、最终 primitive/constraint 数量、结构化错误码和 cleanup 状态。
- 如果请求的 constraint component 名称末尾包含数字段，后续编辑应使用 `create-physics-constraint` 返回的实际 component 名称，因为 Unreal 的 `FName` compaction 可能缩短可见组件名。
