# Search Round 1 Smoke Checklist

Scope: Search Phase 4A v1. This round covers ranked asset search, class-first content search, Blueprint symbol search, level entity search, and unified project search.

Host project: `G:\UEProjects\MyProject`

Validation root: `/Game/UEBridgeMCPValidation/SearchRound1`

Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\SearchRound1\<timestamp>`

## Checklist

- Confirm `tools/list` exposes `search-project`, `search-assets-advanced`, `search-blueprint-symbols`, `search-level-entities`, and `search-content-by-class`.
- Create a timestamped Actor Blueprint fixture under the validation root using `create-blueprint-pattern`.
- Run `search-assets-advanced` against the validation root and verify the fixture asset is returned with a ranked score.
- Run `search-content-by-class` with `class=Blueprint` and verify the same fixture is returned.
- Run `search-blueprint-symbols` for a known generated function, and verify function symbol results.
- Run `search-blueprint-symbols` with `include_nodes=true` for a known custom event, and verify node symbol results include `node_guid`.
- Runtime-seed a temporary editor-world StaticMeshActor with a timestamped label and tag.
- Run `search-level-entities` against the editor world and verify the actor is returned with label, handle, tag, transform, and bounds data.
- Run `search-project` with all three sections enabled and verify assets, Blueprint symbols, and level entities are represented.
- Cover an empty result path with a query that should not match any validation asset.
- Cover a structured negative path by calling `search-content-by-class` without the required `class` argument.
- Clean up the temporary editor-world actor.

## Latest Passing Run

- Timestamp: `20260423_214405`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\SearchRound1\20260423_214405`
- Fixture asset: `/Game/UEBridgeMCPValidation/SearchRound1/BP_SearchRound1_20260423_214405`
- Temporary actor label: `UEBridgeMCP_SearchActor_20260423_214405`

## Notes

- Phase 4A intentionally searches Unreal editor objects and asset registry metadata; it does not add a source-code, config, or arbitrary full-text index.
- Blueprint symbol search loads Blueprints and is capped by `max_blueprints` to protect the GameThread.
- Unified project search returns per-section arrays and a flattened ranked `results[]` list.
