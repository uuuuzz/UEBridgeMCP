# Blueprint Round 2 Provenance

## Scope

This provenance note covers Blueprint Phase 1C only:

- `analyze-blueprint-compile-results`
- `apply-blueprint-fixups`
- `create-blueprint-pattern`
- shared Blueprint compile, finding, and fixup helpers
- documentation and smoke-checklist closure for `MyProject`

It does not claim delivery of Niagara, Physics, Search, Widget patterns, AnimBlueprint patterns, semantic graph rewrites, or multi-round auto-repair.

## Primary Code Sources

- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintCompileUtils.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintCompileUtils.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintFindingUtils.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintFindingUtils.cpp`
- `Source/UEBridgeMCPEditor/Public/Tools/Blueprint/AnalyzeBlueprintCompileResultsTool.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/AnalyzeBlueprintCompileResultsTool.cpp`
- `Source/UEBridgeMCPEditor/Public/Tools/Blueprint/ApplyBlueprintFixupsTool.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/ApplyBlueprintFixupsTool.cpp`
- `Source/UEBridgeMCPEditor/Public/Tools/Blueprint/CreateBlueprintPatternTool.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/CreateBlueprintPatternTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Asset/CompileAssetsTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/AutoFixBlueprintCompileErrorsTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/QueryBlueprintFindingsTool.cpp`
- `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp`

## Documentation Sources

- `Docs/Tools-Reference.md`
- `Docs/Tools-Reference.zh-CN.md`
- `Docs/CapabilityMatrix.md`
- `Docs/CapabilityMatrix.zh-CN.md`
- `Docs/Validation/BlueprintRound2/SmokeChecklist.md`
- `Docs/Validation/BlueprintRound2/SmokeChecklist.zh-CN.md`

## Accepted Runtime Evidence

- Host project: `G:\UEProjects\MyProject`
- Evidence directory: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254`
- Summary: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254\summary.json`
- Runtime registered count in this host session: `102`
- Validation asset root: `/Game/UEBridgeMCPValidation/BlueprintRound2`

Accepted assets:

- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Clean_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Broken_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BPI_R2_Interface_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_InterfaceTarget_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Logic_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Toggle_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Interaction_20260423_171254`

## Validation Summary

- `analyze-blueprint-compile-results` returned clean compile success with zero issues on the clean Blueprint.
- The broken Blueprint produced one compile diagnostic and multiple structural findings.
- `apply-blueprint-fixups(dry_run=true)` returned `status=dry_run` and no modified assets.
- `refresh_all_nodes` followed by `remove_orphan_pins` removed `6` orphan pins.
- `conform_implemented_interfaces` created `Round2InterfaceCheck` and removed `missing_interface_graph`.
- All three curated Actor patterns compiled and saved successfully.
- Existing-path pattern creation returned `UEBMCP_ASSET_ALREADY_EXISTS`.
- `auto-fix-blueprint-compile-errors` still accepted the original four strategies in dry-run mode.

## Known Limits

- `apply-blueprint-fixups` intentionally avoids semantic rewrites such as guessing missing connections or default values.
- `create-blueprint-pattern` v1 only supports Actor Blueprint patterns.
- `analyze-blueprint-compile-results` may compile in memory, but it does not save assets.
