# Blueprint Round 1 Provenance

## Scope

This provenance note covers Blueprint Round 1 only:

- Phase 1A + 1B
- `edit-blueprint-members` member-layer expansion
- `edit-blueprint-graph` graph-layer expansion
- six Blueprint thin-wrapper or query tools
- documentation and smoke-checklist closure for `MyProject`

It does not claim delivery of Phase 1C, compile-result deep analysis, fixup pattern generation, Niagara, Physics, or Search.

## Primary Code Sources

- `Source/UEBridgeMCPEditor/Public/Tools/Write/CreateAssetTool.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Write/CreateAssetTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintToolUtils.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintToolUtils.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/EditBlueprintMembersTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/EditBlueprintGraphTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/CreateBlueprintFunctionTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/CreateBlueprintEventTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/EditBlueprintFunctionSignatureTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/ManageBlueprintInterfacesTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/LayoutBlueprintGraphTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/QueryBlueprintFindingsTool.cpp`
- `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp`

## Documentation Sources

- `Docs/Tools-Reference.md`
- `Docs/Tools-Reference.zh-CN.md`
- `Docs/CapabilityMatrix.md`
- `Docs/CapabilityMatrix.zh-CN.md`
- `Docs/Validation/BlueprintRound1/SmokeChecklist.md`
- `Docs/Validation/BlueprintRound1/SmokeChecklist.zh-CN.md`

## Expected Runtime Evidence

When a host-project smoke run is captured, keep evidence under:

- `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound1\<timestamp>`

Recommended retained artifacts:

- `initialize` response excerpt
- `tools/list` excerpt showing the new Blueprint tools
- request/response samples for wrapper tools and `query-blueprint-findings`
- final validation Blueprint and Blueprint Interface asset paths under `/Game/UEBridgeMCPValidation/BlueprintRound1`
- request/response sample showing the runtime-created interface function declaration used for interface smoke; prefer a non-void signature so `sync_graphs` produces real interface graphs
- compile/save result for the final Blueprint state

## Current Validation Position

Blueprint Round 1 intentionally reuses the existing host project:

- host project: `G:\UEProjects\MyProject`
- validation asset root: `/Game/UEBridgeMCPValidation/BlueprintRound1`
- interface validation is performed with runtime-created Blueprint Interface assets, not repo-tracked fixture assets
- no dedicated automated harness was added for this round

Build compilation is still a required provenance input even before runtime smoke:

- successful editor build in `MyProject`
- new tool registration present in the editor module
- docs updated to describe runtime visibility and intended validation surface

## Known Limits

- `query-blueprint-findings` is structural and non-compiling by design in v1.
- Any later Blueprint Round 2 work should produce separate provenance instead of silently extending this note.
