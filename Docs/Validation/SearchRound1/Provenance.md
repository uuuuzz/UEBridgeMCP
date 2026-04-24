# Search Round 1 Provenance

Date: 2026-04-23

Phase: Search Phase 4A v1

Host project: `G:\UEProjects\MyProject`

Implementation repository: `G:\UEProjects\UEBridgeMCP`

## Implementation

- Added five always-on search tools in the core editor module:
- `search-project`
- `search-assets-advanced`
- `search-blueprint-symbols`
- `search-level-entities`
- `search-content-by-class`
- Added shared search helper code in `Source/UEBridgeMCPEditor/Private/Tools/Search/SearchToolUtils.*` for ranked fuzzy/camel-case scoring, path filters, asset registry collection, Blueprint symbol scanning, and level actor result serialization.
- Registered the tools from `RegisterBuiltInTools()` next to existing project/asset/reference query tools.
- Reused existing actor handle/session serialization and world resolution helpers instead of adding a separate search index service.

## Verification

- Build: `MyProjectEditor Win64 Development` succeeded with `-MaxParallelActions=4`.
- Runtime tool count at `tools/list`: `136`.
- Final evidence directory: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\SearchRound1\20260423_214405`
- Runtime-created validation asset:
- `/Game/UEBridgeMCPValidation/SearchRound1/BP_SearchRound1_20260423_214405`
- Temporary editor-world actor label:
- `UEBridgeMCP_SearchActor_20260423_214405`

## Regressions Covered

- `tools/list` visibility for all five Search Phase 4A tools.
- `create-blueprint-pattern` still creates a searchable Blueprint fixture.
- `search-assets-advanced` returns ranked asset registry matches.
- `search-content-by-class` returns class-filtered Blueprint content and rejects missing `class` with structured `UEBMCP_MISSING_REQUIRED_ARGUMENT`.
- `search-blueprint-symbols` returns generated Blueprint function and custom event node symbols.
- `search-level-entities` returns editor-world actor results with handles and transform/bounds metadata.
- `search-project` aggregates asset, Blueprint symbol, and level entity sections into a flattened ranked result set.

## Boundaries

- This round does not build a persistent search index.
- This round does not search C++ source, config text, logs, or arbitrary project files.
- This round does not do semantic Blueprint graph analysis beyond symbols and optional node metadata.
- This round is read-only except for temporary smoke fixture setup and cleanup.
