# Landscape Round 1 Smoke Checklist

Purpose: close a self-contained Landscape create/query/edit validation loop without relying on existing map fixtures.

Host:

- Project: `G:\UEProjects\MyProject`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\LandscapeRound1\20260423_235345`
- Validation actor label: `LandscapeRound1_20260423_235345`

Checklist:

- Confirm `tools/list` contains `query-landscape-summary`, `create-landscape`, `edit-landscape-region`, `edit-level-batch`, and `get-project-info`.
- Confirm `get-project-info.optional_capabilities.landscape_available=true`.
- Query the editor world with `query-landscape-summary` to capture the baseline.
- Run `create-landscape` with `dry_run=true` and confirm no assets are modified.
- Run `create-landscape` live with a 1x1 component, 1 section per component, and 7 quads per section.
- Query the created Landscape with `include_components=true` and `sample_points=[[1,1]]`; confirm one Landscape, one component, 8x8 resolution, and initial local height `0`.
- Run `edit-landscape-region` with `dry_run=true` over `[0,0] -> [2,2]` and `delta=128`; confirm no modified assets and height remains `0`.
- Run `edit-landscape-region` live with the same region and delta.
- Query height samples at `[1,1]` and `[4,4]`; confirm the edited point changed from `0` to `128` and the outside-region point remained `0`.
- Query a missing Landscape actor and confirm structured `UEBMCP_LANDSCAPE_NOT_FOUND`.
- Delete the temporary Landscape through `edit-level-batch(delete_actor)` and confirm a follow-up query by actor name fails.

Final evidence:

- `summary.json`: all validation flags passed.
- `create-landscape-dry-run.json`: dry-run succeeded and returned no `modified_assets`.
- `create-landscape.json`: created a 1-component Landscape with 8x8 resolution.
- `query-created.json`: returned the created Landscape, component details, and initial height sample.
- `edit-landscape-dry-run.json`: dry-run succeeded without height changes.
- `edit-landscape-apply.json`: region edit reported `changed=true`.
- `query-after-apply.json`: height sample inside the edited region was `128`; outside-region sample remained `0`.
- `negative-missing-landscape.json`: structured missing Landscape failure.
- `cleanup-delete-landscape.json`: temporary Landscape actor cleanup succeeded.
