# Landscape Round 1 Provenance

This round was added after Foliage Round 1 to close the other high-value World Production gap that still had a write tool but no self-contained create/query verification loop.

Implementation changes:

- `query-landscape-summary` was added as a conditional Landscape query tool. It reports Landscape actors, transform, bounds, Landscape GUID, component sizing, layer settings, optional component summaries, and optional landscape-coordinate height samples.
- `create-landscape` was added as a conditional Landscape creation tool for small flat editor-world Landscape actors. It supports dry-run, component count, sections per component, quads per section, initial local height, transform, and save control.
- Shared Landscape serialization lives in `LandscapeToolUtils`, including height sampling through `FLandscapeEditDataInterface`.
- `edit-landscape-region` was validated against the new query samples so the smoke proves data mutation, not just a successful response.
- A UE 5.7 linker compatibility issue was avoided by reading weightmap layer names through `ULandscapeLayerInfoObject` rather than calling the non-exported `FWeightmapLayerAllocationInfo::GetLayerName()` helper.

Validation run:

- Timestamp: `20260423_235345`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\LandscapeRound1\20260423_235345`
- Created actor label: `LandscapeRound1_20260423_235345`
- Created resolution: `8x8`
- Height proof: sample `[1,1]` changed from `0` to `128`; sample `[4,4]` remained `0`.

Build:

- Command: `Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- Result: succeeded.

Scope boundaries:

- This round covers small flat Landscape creation, structural summaries, component/layer reporting, height sampling, rectangular height edits, dry-run behavior, structured missing-actor errors, and cleanup.
- It does not add landscape material graph authoring, layer info asset creation, sculpt brush simulation, spline-to-landscape deformation, streaming proxy management, or World Partition landscape region generation.
