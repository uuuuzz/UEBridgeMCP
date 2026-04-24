# Foliage Round 1 Provenance

本轮是在 Sequencer 验证之后继续补世界生产长尾能力时加入的。目标是一条能在 World Partition editor world 中稳定运行、且不依赖预置项目 fixture 的 foliage 工作流。

实现变更：

- 新增条件工具 `query-foliage-summary`，用于按 foliage type 和 static mesh 汇总 editor 或 PIE world 中的 foliage，支持按 foliage type 或 mesh 过滤，并可选返回实例 transform 采样。
- `create-asset` 新增 `asset_class="FoliageType_InstancedStaticMesh"`，通过 `static_mesh_path` 创建保存型 `UFoliageType_InstancedStaticMesh` 资产。
- `edit-foliage-batch` 的 dry-run add 校验不再创建或要求 `AInstancedFoliageActor`。
- `edit-foliage-batch` 避开 World Partition 下不安全的 `AInstancedFoliageActor::AddMesh(UStaticMesh*)` 路径；在 partitioned world 中，真实写入应使用保存型 `foliage_type_path`。
- `query-foliage-summary` 将 package path 与 object path 视为等价过滤条件，因此调用者可以传 `/Game/Path/Asset`，而返回摘要仍可报告 `/Game/Path/Asset.Asset`。

验证运行：

- 时间戳：`20260423_233538`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\FoliageRound1\20260423_233538`
- 创建的 foliage type：`/Game/UEBridgeMCPValidation/FoliageRound1/FT_FoliageRound1_20260423_233538`
- Static mesh：`/Engine/BasicShapes/Cube.Cube`

构建：

- 命令：`Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- 结果：成功。

范围边界：

- 本轮覆盖 foliage type 创建、summary 查询、dry-run 校验、实例 add/remove，以及缺失 mesh 的结构化错误。
- 本轮不新增 procedural foliage volume authoring、foliage painting UI 自动化、PCG graph authoring、landscape-grass 集成、foliage density/scalability preset 或 runtime foliage simulation 断言。
