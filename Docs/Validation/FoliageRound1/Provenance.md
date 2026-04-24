# Foliage Round 1 Provenance

This round was added after Sequencer validation, as part of closing long-tail world-production authoring loops. The practical target was a foliage workflow that works in World Partition editor worlds without relying on pre-existing project fixtures.

Implementation changes:

- `query-foliage-summary` was added as a conditional Foliage query tool. It summarizes editor or PIE foliage by foliage type and static mesh, supports optional foliage type or mesh filtering, and can include bounded instance transform samples.
- `create-asset` now supports `asset_class="FoliageType_InstancedStaticMesh"` with `static_mesh_path`, creating a saved `UFoliageType_InstancedStaticMesh` asset.
- `edit-foliage-batch` dry-run add validation no longer creates or requires an `AInstancedFoliageActor`.
- `edit-foliage-batch` avoids the unsafe World Partition path where `AInstancedFoliageActor::AddMesh(UStaticMesh*)` asserts. On partitioned worlds, real writes should use a saved `foliage_type_path`.
- `query-foliage-summary` treats package paths and object paths as equivalent filters, so callers can pass `/Game/Path/Asset` while summaries still report `/Game/Path/Asset.Asset`.

Validation run:

- Timestamp: `20260423_233538`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\FoliageRound1\20260423_233538`
- Created foliage type: `/Game/UEBridgeMCPValidation/FoliageRound1/FT_FoliageRound1_20260423_233538`
- Static mesh: `/Engine/BasicShapes/Cube.Cube`

Build:

- Command: `Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- Result: succeeded.

Scope boundaries:

- This round covers foliage type creation, summary query, dry-run validation, instance add/remove, and structured missing-mesh errors.
- It does not add procedural foliage volume authoring, foliage painting UI automation, PCG graph authoring, landscape-grass integration, foliage density/scalability presets, or runtime foliage simulation assertions.
