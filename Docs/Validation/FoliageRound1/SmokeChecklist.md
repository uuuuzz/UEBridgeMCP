# Foliage Round 1 Smoke Checklist

Purpose: close a self-contained foliage create/query/edit validation loop that is safe on World Partition editor worlds and does not require repo-tracked `.uasset` fixtures.

Host:

- Project: `G:\UEProjects\MyProject`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\FoliageRound1\20260423_233538`
- Validation asset root: `/Game/UEBridgeMCPValidation/FoliageRound1`

Checklist:

- Confirm `tools/list` contains `query-foliage-summary`, `edit-foliage-batch`, `create-asset`, and `get-project-info`.
- Confirm `get-project-info.optional_capabilities.foliage_available=true`.
- Create a saved foliage type asset through `create-asset(asset_class="FoliageType_InstancedStaticMesh", static_mesh_path="/Engine/BasicShapes/Cube.Cube")`.
- Query the new foliage type through `query-foliage-summary` and confirm the initial instance count is `0`.
- Run `edit-foliage-batch` with `dry_run=true`, `mesh_path`, and `add_instances` before any InstancedFoliageActor exists; confirm the request succeeds and no modified assets are reported.
- Query the foliage type after dry-run and confirm the instance count remains `0`.
- Add two instances with `edit-foliage-batch` using `foliage_type_path`, not `mesh_path`, to avoid World Partition `AddMesh(UStaticMesh*)` assertions.
- Query the foliage type with `include_instances=true` and confirm `foliage_type_count=1`, `instance_count=2`, and two sampled instance transforms are returned.
- Attempt to add instances from a missing mesh path and confirm a structured negative result.
- Remove the two instances with `remove_instances_in_bounds` and confirm the final instance count is `0`.

Final evidence:

- `summary.json`: all validation flags passed.
- `create-foliage-type.json`: `create-asset` returned `created_class="FoliageType_InstancedStaticMesh"`.
- `edit-foliage-dry-run-mesh-add.json`: dry-run succeeded, reported `added_count=2`, and returned no `modified_assets`.
- `query-after-dry-run-type.json`: dry-run left the foliage type at `instance_count=0`.
- `edit-foliage-add-type.json`: real add by `foliage_type_path` reported `added_count=2`.
- `query-after-add-type.json`: query returned one foliage type, two instances, and two sampled transforms.
- `negative-missing-mesh.json`: missing mesh path returned a structured failure.
- `edit-foliage-remove-type.json`: bounds cleanup removed both instances.
