# Foliage Round 1 Smoke Checklist

目的：关闭一条自包含的 foliage 创建、查询、编辑验证闭环，并确保它在 World Partition editor world 中安全，不依赖仓库内 `.uasset` fixture。

宿主：

- 工程：`G:\UEProjects\MyProject`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\FoliageRound1\20260423_233538`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/FoliageRound1`

Checklist：

- 确认 `tools/list` 包含 `query-foliage-summary`、`edit-foliage-batch`、`create-asset` 和 `get-project-info`。
- 确认 `get-project-info.optional_capabilities.foliage_available=true`。
- 通过 `create-asset(asset_class="FoliageType_InstancedStaticMesh", static_mesh_path="/Engine/BasicShapes/Cube.Cube")` 创建保存型 foliage type 资产。
- 通过 `query-foliage-summary` 查询新 foliage type，并确认初始 `instance_count=0`。
- 在还没有 InstancedFoliageActor 的情况下，用 `edit-foliage-batch` 执行 `dry_run=true`、`mesh_path`、`add_instances`，确认请求成功并且没有 `modified_assets`。
- dry-run 后再次查询 foliage type，确认实例数量仍为 `0`。
- 使用 `foliage_type_path`，而不是 `mesh_path`，通过 `edit-foliage-batch` 添加 2 个实例，避免 World Partition 中的 `AddMesh(UStaticMesh*)` 断言路径。
- 使用 `include_instances=true` 查询 foliage type，确认 `foliage_type_count=1`、`instance_count=2`，并返回 2 个实例 transform 采样。
- 对缺失 mesh path 执行 add_instances，确认返回结构化失败。
- 用 `remove_instances_in_bounds` 删除 2 个实例，并确认最终 `instance_count=0`。

最终证据：

- `summary.json`：全部 validation flag 通过。
- `create-foliage-type.json`：`create-asset` 返回 `created_class="FoliageType_InstancedStaticMesh"`。
- `edit-foliage-dry-run-mesh-add.json`：dry-run 成功，报告 `added_count=2`，且没有 `modified_assets`。
- `query-after-dry-run-type.json`：dry-run 后 foliage type 仍为 `instance_count=0`。
- `edit-foliage-add-type.json`：通过 `foliage_type_path` 的真实 add 返回 `added_count=2`。
- `query-after-add-type.json`：查询返回 1 个 foliage type、2 个实例、2 个 transform 采样。
- `negative-missing-mesh.json`：缺失 mesh path 返回结构化失败。
- `edit-foliage-remove-type.json`：bounds 清理删除了 2 个实例。
