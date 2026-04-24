# UEBridgeMCP Tools Reference

Source of truth:

- The live runtime inventory is whatever the editor returns from `tools/list`.
- The live count is whatever `initialize.capabilities.tools.registeredCount` reports.
- `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp :: RegisterBuiltInTools()` is the base editor registration source.
- Step 6 added protocol-level `resources/*` and `prompts/*`, plus conditional and extension-module tools, so the runtime count is intentionally dynamic.

Current inventory model:

- Base editor surface: always-on tools registered directly from `RegisterBuiltInTools()`.
- Additional core conditional tools may be registered when the related engine modules are available:
  - `query-level-sequence-summary`
  - `edit-sequencer-tracks`
  - `query-landscape-summary`
  - `create-landscape`
  - `edit-landscape-region`
  - `query-foliage-summary`
  - `edit-foliage-batch`
  - `query-worldpartition-cells`
  - `query-niagara-system-summary`
  - `query-niagara-emitter-summary`
  - `create-niagara-system-from-template`
  - `edit-niagara-user-parameters`
  - `apply-niagara-system-to-actor`
  - `query-metasound-summary`
  - `create-metasound-source`
  - `edit-metasound-graph`
  - `set-metasound-input-defaults`
- Additional extension-module tools may appear when the corresponding modules are loaded:
  - `edit-control-rig-graph`
  - `generate-pcg-scatter`
  - `query-pcg-graph-summary`
  - `edit-pcg-graph`
  - `run-pcg-graph`
  - `generate-external-content`
  - `generate-external-asset`

Do not treat any single fixed number as authoritative after Step 6. For release gating, run `Validation/Smoke/Invoke-ReleasePreflight.ps1`; it records the live `registeredCount`, `tools/list`, resources, prompts, compatibility aliases, and safety probes in `Tmp/Validation/ReleasePreflight/<timestamp>/summary.json`.

## Protocol Surface

UEBridgeMCP now exposes all of these MCP methods:

- `initialize`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/read`
- `prompts/list`
- `prompts/get`

`initialize` advertises:

- `capabilities.tools`
- `capabilities.resources`
- `capabilities.prompts`
- `capabilities.tools.registeredCount`

## Built-in Resources

Built-in resources are repo-tracked text files under `Resources/MCP/Resources/` and are read-only at runtime.

| URI | Name | Purpose |
|---|---|---|
| `uebmcp://builtin/resources/animation-smoke-checklist` | Animation Smoke Checklist | Minimal animation authoring and regression checklist |
| `uebmcp://builtin/resources/sequencer-edit-recipe` | Sequencer Edit Recipe | Safe Sequencer edit order and validation recipe |
| `uebmcp://builtin/resources/world-production-recipe` | World Production Recipe | Practical world-production workflow guidance |
| `uebmcp://builtin/resources/performance-triage-guide` | Performance Triage Guide | Structured performance triage and evidence capture guide |
| `uebmcp://builtin/resources/external-content-safety-guide` | External Content Safety Guide | Provenance and safety guidance for external content workflows |

## Built-in Prompts

Built-in prompts are repo-tracked JSON templates under `Resources/MCP/Prompts/`.

| Prompt name | Purpose | Key arguments |
|---|---|---|
| `animation-workflow-brief` | Compose a concise animation workflow brief | `goal`, `asset_path`, `notes` |
| `sequencer-edit-brief` | Compose a safe Sequencer edit brief | `goal`, `sequence_path`, `shot_notes` |
| `performance-triage-brief` | Compose a lightweight performance investigation brief | `goal`, `world_mode`, `focus_area` |

## Workflow Presets

Step 6 added project workflow presets:

- `manage-workflow-presets`
- `run-workflow-preset`

Preset files live under `Config/UEBridgeMCP/WorkflowPresets/*.json`.

Preset schema includes:

- `id`
- `title`
- `description`
- `resource_uris[]`
- `prompt_name`
- `tool_calls[]`
- `default_arguments{}`

`run-workflow-preset` supports:

- `dry_run=true` to expand resources, prompt, and resolved tool plan without execution
- sequential tool execution when `dry_run=false`

## Base Tool Surface

The base editor surface below is always registered by `RegisterBuiltInTools()`.

### 1. Blueprint Query

- `query-blueprint-summary`
- `query-blueprint-graph-summary`
- `query-blueprint-node`
- `query-blueprint-findings`
- `analyze-blueprint-compile-results`

### 2. Animation And Performance Query

- `query-animation-asset-summary`
- `query-skeleton-summary`
- `query-performance-report`
- `capture-performance-snapshot`
- `query-render-stats`
- `query-memory-report`
- `profile-visible-actors`
- `query-physics-summary`

### 3. Level And World Query

- `query-level-summary`
- `query-actor-detail`
- `query-actor-selection`
- `query-spatial-context`
- `query-world-summary`

### 4. Material, StaticMesh, Audio, And Environment Query

- `query-material-summary`
- `query-material-instance`
- `query-static-mesh-summary`
- `query-mesh-complexity`
- `query-audio-asset-summary`
- `query-environment-summary`

### 5. Project, Asset, And Utility Query

- `get-project-info`
- `query-asset`
- `query-datatable`
- `get-asset-diff`
- `get-class-hierarchy`
- `query-engine-api-symbol`
- `query-class-member-summary`
- `query-plugin-capabilities`
- `query-editor-subsystem-summary`
- `query-workspace-health`
- `find-references`
- `search-project`
- `search-assets-advanced`
- `search-blueprint-symbols`
- `search-level-entities`
- `search-content-by-class`
- `query-unused-assets`
- `inspect-widget-blueprint`
- `get-logs`

### 6. Widget And UMG Authoring

- `create-widget-blueprint`
- `edit-widget-blueprint`
- `edit-widget-layout-batch`
- `edit-widget-animation`
- `edit-widget-component`
- `create-common-ui-widget`
- `edit-common-ui`
- `query-common-ui-widgets`

Compatibility note:

- `add-widget` is still registered for compatibility, but new write flows should prefer the batched widget tools.

### 7. Create And Data Authoring

- `create-asset`
- `create-user-defined-struct`
- `create-user-defined-enum`
- `add-component`
- `add-widget`
- `add-datatable-row`
- `spawn-actor`

Create note:

- `create-asset` now explicitly supports `BlueprintInterface`, `LevelSequence`, and `FoliageType_InstancedStaticMesh` in addition to Blueprint, WidgetBlueprint, AnimBlueprint, Material, DataTable, DataAsset, and generic class-path-based creation.
- Use `asset_class="BlueprintInterface"` with a normal asset path such as `/Game/UEBridgeMCPValidation/BlueprintRound1/BPI_BlueprintRound1_20260423_123000`.
- `parent_class` is ignored for `BlueprintInterface`; interface assets are created through the engine's dedicated Blueprint Interface factory.
- Use `asset_class="AnimBlueprint"` with `parent_class` set to a Skeleton asset path. AnimBlueprint creation now uses the engine `UAnimBlueprintFactory`, so the result is a real `AnimBlueprint` asset with a target skeleton and default animation graph.
- Use `asset_class="LevelSequence"` to create an initialized Level Sequence asset. The asset is initialized through `ULevelSequence::Initialize()`, so it has a real MovieScene before later Sequencer edits run.
- Use `asset_class="FoliageType_InstancedStaticMesh"` with `static_mesh_path` to create a saved foliage type asset. This is the preferred World Partition-safe setup before adding foliage with `edit-foliage-batch` and `foliage_type_path`.

### 8. StateTree

- `query-statetree`
- `add-statetree-state`
- `remove-statetree-state`
- `add-statetree-transition`
- `add-statetree-task`
- `edit-statetree-bindings`

### 9. Gameplay, Input, AI, Navigation, Networking

- `create-input-action`
- `create-input-mapping-context`
- `edit-input-mapping-context`
- `manage-gameplay-tags`
- `query-gas-asset-summary`
- `create-gameplay-ability`
- `create-gameplay-effect`
- `create-attribute-set`
- `edit-gameplay-effect-modifiers`
- `manage-ability-system-bindings`
- `query-ability-system-state`
- `create-gameframework-blueprint-set`
- `create-ai-behavior-assets`
- `query-navigation-state`
- `query-navigation-path`
- `edit-navigation-build`
- `query-ai-behavior-assets`
- `edit-blackboard-keys`
- `query-replication-summary`
- `edit-replication-settings`
- `query-network-component-settings`
- `edit-network-component-settings`
- `trace-gameplay-collision`
- `edit-collision-settings`
- `edit-physics-simulation`
- `create-physics-constraint`
- `edit-physics-constraint`

### 10. Scripting, Build, PIE, Reflection

- `run-python-script`
- `trigger-live-coding`
- `build-and-relaunch`
- `pie-session`
- `pie-input`
- `wait-for-world-condition`
- `assert-world-state`
- `query-runtime-actor-state`
- `query-ability-system-state`
- `trace-gameplay-collision`
- `call-function`
- `edit-editor-selection`
- `edit-viewport-camera`
- `run-editor-command`

### 11. Batch Edit And Asset Lifecycle

- `edit-blueprint-graph`
- `edit-blueprint-members`
- `create-blueprint-function`
- `create-blueprint-event`
- `edit-blueprint-function-signature`
- `manage-blueprint-interfaces`
- `layout-blueprint-graph`
- `apply-blueprint-fixups`
- `edit-blueprint-components`
- `add-graph-node`
- `connect-graph-pins`
- `disconnect-graph-pin`
- `remove-graph-node`
- `set-property`
- `edit-datatable-batch`
- `edit-level-actor`
- `edit-level-batch`
- `align-actors-batch`
- `drop-actors-to-surface`
- `edit-static-mesh-settings`
- `edit-static-mesh-slots`
- `replace-static-mesh`
- `edit-material-instance-batch`
- `create-material-instance`
- `edit-material-graph`
- `create-sound-cue`
- `edit-sound-cue-routing`
- `create-audio-component-setup`
- `apply-audio-to-actor`
- `edit-gameplay-effect-modifiers`
- `manage-ability-system-bindings`
- `edit-environment-lighting`
- `create-animation-montage`
- `create-blend-space`
- `edit-blend-space-samples`
- `edit-animation-notifies`
- `edit-anim-graph-node`
- `edit-anim-blueprint-state-machine`
- `compile-assets`
- `manage-assets`
- `manage-asset-folders`
- `import-assets`
- `source-control-assets`
- `capture-viewport`
- `apply-material`
- `apply-physical-material`
- `edit-spline-actors`

### 12. Workflow And High-Level Orchestration

- `manage-workflow-presets`
- `run-workflow-preset`
- `run-editor-macro`
- `run-project-maintenance-checks`
- `blueprint-scaffold-from-spec`
- `create-blueprint-pattern`
- `generate-blueprint-pattern`
- `query-gameplay-state`
- `auto-fix-blueprint-compile-errors`
- `generate-level-structure`
- `generate-level-pattern`

Blueprint Phase 1C note:

- `analyze-blueprint-compile-results` compiles a Blueprint in memory, merges compile diagnostics with static Blueprint findings, and returns normalized `issues[]` plus `suggested_fixups`.
- `apply-blueprint-fixups` applies safe structural fixups: `refresh_all_nodes`, `reconstruct_invalid_nodes`, `remove_orphan_pins`, `recompile_dependencies`, and `conform_implemented_interfaces`.
- `create-blueprint-pattern` is the curated high-level Actor pattern entrypoint for `logic_actor_skeleton`, `toggle_state_actor`, and `interaction_stub_actor`.
- `blueprint-scaffold-from-spec` remains a lower-level spec-driven scaffold tool and is not the backend for curated Phase 1C patterns.

Niagara Phase 2A note:

- Niagara tools are conditionally registered from the core editor module when the engine `Niagara` and `NiagaraEditor` modules are available.
- `query-niagara-system-summary` and `query-niagara-emitter-summary` cover system/emitter summaries, exposed user parameters, enabled emitter handles, and renderer summaries.
- `create-niagara-system-from-template` creates a new Niagara system either from Niagara's editor factory or by duplicating an existing system template.
- `edit-niagara-user-parameters` supports batched add/remove/rename/default-value edits for v1 scalar/vector/color user parameters.
- `apply-niagara-system-to-actor` creates or updates an actor NiagaraComponent and can apply component-level user parameter overrides; editor-world `activate_now` requests are deferred with a warning instead of starting simulation on the editor GameThread.

Audio Phase 2B note:

- Audio tools are always registered from the core editor module and use engine `AudioEditor` support for SoundCue creation.
- `query-audio-asset-summary` covers `SoundWave` and `SoundCue` metadata, including cue node routing summaries.
- `create-sound-cue` creates a new SoundCue from an optional `initial_sound_wave_path` and basic volume/pitch settings.
- `edit-sound-cue-routing` supports batched `operations[]` for wave-player, random, mixer, attenuation wrapping, multiplier edits, dry-run, rollback, and save paths.
- `create-audio-component-setup` and `apply-audio-to-actor` create or update actor `AudioComponent` setup; editor-world `play_now` requests are deferred with a warning instead of starting playback on the editor GameThread.

MetaSound Phase 2C note:

- MetaSound tools are conditionally registered from the core editor module when `MetasoundEngine`, `MetasoundFrontend`, and `MetasoundEditor` are available.
- `query-metasound-summary` summarizes MetaSound Source assets, graph inputs/outputs, interfaces, and optional default-graph nodes/edges.
- `create-metasound-source` creates a new MetaSound Source through the engine Source Builder, with optional v1 graph inputs and output format selection.
- `set-metasound-input-defaults` supports batched bool/int32/float/string graph input default edits, including dry-run, rollback, and save paths.
- `edit-metasound-graph` is intentionally restricted to v1 structural edits: graph I/O, class-name node insertion, explicit connections, node input defaults, and layout. Arbitrary MetaSound graph synthesis remains future work.

Physics Phase 3A note:

- Physics tools are always registered from the core editor module and use engine `PhysicsCore` plus editor-world actor/component APIs.
- `query-physics-summary` returns world or actor physics summaries, including PrimitiveComponent collision/simulation state and PhysicsConstraintComponent details.
- `edit-collision-settings` supports collision profile, enabled mode, object channel, all-channel response, and per-channel response edits with dry-run, rollback, and save paths.
- `edit-physics-simulation` supports simulate physics, gravity, mass override, mass scale, damping, mobility adjustment, wake, and sleep edits.
- `create-physics-constraint` and `edit-physics-constraint` cover basic two-component constraints, disable-collision, projection, linear limits, angular limits, and break thresholds.
- `apply-physical-material` applies a PhysicalMaterial override to a PrimitiveComponent; runtime physics playback/simulation assertions remain future work.

GAS Phase 3B note:

- GAS tools are always registered from the core editor module and use engine `GameplayAbilities`, `GameplayAbilitiesEditor`, and `GameplayTasks` support.
- `create-attribute-set` creates AttributeSet Blueprint assets and seeds `FGameplayAttributeData` member variables.
- `create-gameplay-effect` creates GameplayEffect Blueprint assets with duration, period, granted target tags, and simple constant modifiers.
- `edit-gameplay-effect-modifiers` supports dry-run and rollback-safe edits for duration, period, granted tags, clear/add/remove modifiers, and final compile/save.
- `create-gameplay-ability` creates GameplayAbility Blueprint assets with asset tags, activation tag containers, common policy fields, cost effect, and cooldown effect defaults.
- `query-gas-asset-summary` summarizes GameplayAbility, GameplayEffect, AttributeSet, and Actor Blueprint GAS bindings.
- `manage-ability-system-bindings` adds an `AbilitySystemComponent` to Actor Blueprints and stores ability/effect/attribute-set class bindings as categorized `TSubclassOf` variables with UEBridgeMCP metadata.
- v1 intentionally stops at asset and Actor Blueprint configuration; runtime granting, prediction validation, complex execution calculations, and ability task graph authoring remain future work.

Gameplay Runtime Phase 3C note:

- `query-runtime-actor-state` returns read-only editor or PIE actor runtime state: transform, bounds, velocity, actor tags, components, collision summaries, and optional GAS state.
- `query-ability-system-state` returns live `AbilitySystemComponent` state for editor or PIE actors, including owned gameplay tags, spawned AttributeSets, and activatable abilities.
- `trace-gameplay-collision` runs read-only line, sphere, capsule, or box traces in editor or PIE worlds and returns structured hit actor/component data.
- Phase 3C validates live PIE querying and collision traces, but does not grant abilities, activate abilities, validate prediction, or assert long-running runtime physics simulation.

Search Phase 4A note:

- `search-assets-advanced` provides ranked asset search with exact, wildcard, contains, camel-case, and fuzzy subsequence matching across asset name/path/class fields.
- `search-content-by-class` is the class-first asset search entrypoint and keeps `class` required while still supporting query and path filters.
- `search-blueprint-symbols` scans Blueprint variables, function graphs, macro graphs, event graphs, and optionally graph nodes with result caps to avoid long GameThread stalls.
- `search-level-entities` searches editor or PIE actors by label/name/class/folder/tag and returns actor handles plus optional transform/bounds data.
- `search-project` aggregates assets, Blueprint symbols, and level entities into per-section results plus a flattened ranked list.

Engine API Phase 4B note:

- `query-engine-api-symbol` searches loaded local reflection data for classes, functions, properties, structs, enums, subsystems, and plugins.
- `query-class-member-summary` resolves a class name or path and returns structured function/property summaries with flags, parameter data, and optional metadata.
- `query-plugin-capabilities` reports local plugin enablement, mounted/content/Verse support, descriptor metadata, and module loading state.
- `query-editor-subsystem-summary` lists Editor/Engine/World/GameInstance subsystem classes and reports current instance availability where the relevant context exists.
- Phase 4B is intentionally local-only: it does not proxy external documentation sites or build an online docs mirror.

Macro / Utility Phase 4C note:

- `query-workspace-health` returns a read-only snapshot of project paths, UEBridgeMCP plugin/server state, editor/PIE world availability, optional capabilities, validation path existence, and dirty packages.
- `run-project-maintenance-checks` bundles curated maintenance checks: workspace health, conservative unused-asset candidates, and optional Blueprint compile checks.
- `generate-blueprint-pattern` is a workflow-facing wrapper over `create-blueprint-pattern`; it keeps the same curated Actor catalog and adds a dry-run plan path.
- `generate-level-pattern` creates engine-only editor-world scaffold patterns: `test_anchor_pair`, `interaction_test_lane`, and `lighting_blockout_minimal`.
- `run-editor-macro` only exposes a curated macro catalog: `collect_workspace_health`, `run_maintenance_checks`, `compile_blueprint_assets`, and `cleanup_generated_actors`.
- Phase 4C intentionally does not add a universal script executor. Use `run-python-script` only as the existing explicit scripting tool, not as the backend for curated macros.

Animation Round 2 closure note:

- `edit-anim-blueprint-state-machine` now supports `create_state_machine` and `ensure_state_machine` operations, including optional `connect_to_output`, so positive state-machine smoke can be created from public MCP tools instead of depending on a prebuilt AnimBP fixture.
- The self-contained host smoke creates an AnimBlueprint through `create-asset(asset_class="AnimBlueprint")`, creates a `Locomotion` state machine, adds two states, sets the entry state, adds a transition, assigns a sequence, queries the summary, and compiles the asset.
- Evidence: `Tmp/Validation/AnimationRound2/20260423_223834/summary.json`.

Sequencer Round 1 closure note:

- `create-asset(asset_class="LevelSequence")` now creates a real initialized `ULevelSequence` with a MovieScene, so Sequencer smoke does not depend on prebuilt sequence assets.
- `query-level-sequence-summary` reports playback range, frame rates, bindings, possessables, spawnables, master/binding tracks, sections, and camera cut details.
- `edit-sequencer-tracks` now uses the engine camera-cut path instead of creating a Camera Cut as an ordinary master track, so `GetCameraCutTrack()` and the summary agree.
- Evidence: `Tmp/Validation/SequencerRound1/20260423_225804/summary.json`.

## Core Conditional Editor Tools

These are registered from the core editor module only when the related engine modules are available.

| Tool | Typical dependency | Notes |
|---|---|---|
| `query-level-sequence-summary` | `LevelSequence`, `MovieSceneTracks` | Summarizes playback range, bindings, tracks, sections, and camera cuts |
| `edit-sequencer-tracks` | `LevelSequence`, `MovieSceneTracks` | v1 focuses on bindings, Camera Cuts, basic tracks, sections, keys, and playback ranges |
| `query-landscape-summary` | `Landscape` | Summarizes Landscape actors, components, layers, bounds, and optional height samples |
| `create-landscape` | `Landscape` | Creates small flat editor-world Landscape actors for blockouts and validation fixtures |
| `edit-landscape-region` | `Landscape` | v1 supports rectangular-region terrain and layer edits |
| `query-foliage-summary` | `Foliage` | Summarizes editor or PIE foliage by foliage type and static mesh, with optional instance samples |
| `edit-foliage-batch` | `Foliage` | v1 supports add, remove in bounds, transform edits, and foliage mesh replacement |
| `query-worldpartition-cells` | Engine World Partition support | Returns structured no-op or unsupported results on non-World-Partition maps |
| `edit-worldpartition-cells` | Engine World Partition support | Loads or unloads World Partition runtime cells where engine support is available |
| `query-niagara-system-summary` | `Niagara`, `NiagaraEditor` | Summarizes Niagara systems, emitters, renderers, readiness, and user parameters |
| `query-niagara-emitter-summary` | `Niagara`, `NiagaraEditor` | Summarizes an emitter asset or a system emitter handle |
| `create-niagara-system-from-template` | `Niagara`, `NiagaraEditor` | Creates a Niagara system from a template or factory-backed empty/default system |
| `edit-niagara-user-parameters` | `Niagara`, `NiagaraEditor` | Batched v1 user parameter editing with dry-run, compile, and save support |
| `apply-niagara-system-to-actor` | `Niagara`, `NiagaraEditor` | Adds or updates a NiagaraComponent on an editor-world actor |
| `query-metasound-summary` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | Summarizes MetaSound Source assets and optional default-graph structure |
| `create-metasound-source` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | Creates a MetaSound Source through the official Source Builder |
| `edit-metasound-graph` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | Restricted v1 MetaSound graph I/O, connection, default, and layout edits |
| `set-metasound-input-defaults` | `MetasoundEngine`, `MetasoundFrontend`, `MetasoundEditor` | Batch edits supported graph input defaults with dry-run and rollback |

## Extension-Module Tools

These tools are intentionally kept outside the core editor module.

| Tool | Module | Notes |
|---|---|---|
| `edit-control-rig-graph` | `UEBridgeMCPControlRig` | Optional Control Rig editor graph editing |
| `generate-pcg-scatter` | `UEBridgeMCPPCG` | Optional PCG scatter setup using an existing graph or a scaffold graph asset |
| `query-pcg-graph-summary` | `UEBridgeMCPPCG` | Summarizes PCG graph nodes, pins, edges, and settings |
| `edit-pcg-graph` | `UEBridgeMCPPCG` | Adds, removes, edits, and connects PCG graph nodes in a bounded v1 surface |
| `run-pcg-graph` | `UEBridgeMCPPCG` | Triggers PCG component generation for selected actors or actor paths |
| `generate-external-content` | `UEBridgeMCPExternalAI` | Optional HTTP/JSON external text or JSON generation tool |
| `generate-external-asset` | `UEBridgeMCPExternalAI` | Produces external asset payloads and explicit import plans without writing imported assets in v1 |

`generate-external-content` is intentionally extension-scoped:

- It is not part of the core editor tool surface.
- It uses a provider/settings layer.
- v1 supports text and JSON outputs only.
- It does not download or import binary media.

## Optional Capability Reporting

`get-project-info` now returns an `optional_capabilities` object with at least:

- `sequencer_available`
- `control_rig_available`
- `landscape_available`
- `foliage_available`
- `world_partition_available`
- `pcg_available`
- `niagara_available`
- `metasound_available`
- `external_ai_available`

Clients should use that object together with `tools/list` instead of assuming any fixed Step 6 tool count.

## Compatibility Aliases

UEBridgeMCP now registers a name-only compatibility alias layer for common `snake_case` tool names.

- Alias calls resolve through `FMcpToolRegistry::ResolveToolName()`.
- `tools/list` includes alias definitions whose descriptions identify the canonical target.
- Canonical UEBridgeMCP schemas remain authoritative; aliases do not rewrite legacy argument payloads.
- Static smoke coverage lives at `Validation/Smoke/VerifyCompatibilityAliases.ps1`.
- Release preflight coverage lives at `Validation/Smoke/Invoke-ReleasePreflight.ps1` and adds runtime alias calls plus safety probes.

## Checking The Live Inventory

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"check","version":"1.0"}}}'
```

Then:

```bash
curl -s -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json,text/event-stream" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
```

If the count differs between machines, that is expected after Step 6 when optional capabilities or extension modules differ.

## See Also

- [Tool Development Guide](./ToolDevelopment.md)
- [Architecture](./Architecture.md)
- [Troubleshooting](./Troubleshooting.md)
- [Capability Matrix](./CapabilityMatrix.md)
