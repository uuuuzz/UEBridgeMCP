# Blueprint Round 2 Provenance

## 范围

本 provenance 只覆盖 Blueprint Phase 1C：

- `analyze-blueprint-compile-results`
- `apply-blueprint-fixups`
- `create-blueprint-pattern`
- 共享 Blueprint compile、finding、fixup helper
- `MyProject` 宿主 smoke checklist 与文档闭环

它不声明交付 Niagara、Physics、Search、Widget pattern、AnimBlueprint pattern、语义级图重写或多轮自动修复。

## 主要代码来源

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

## 文档来源

- `Docs/Tools-Reference.md`
- `Docs/Tools-Reference.zh-CN.md`
- `Docs/CapabilityMatrix.md`
- `Docs/CapabilityMatrix.zh-CN.md`
- `Docs/Validation/BlueprintRound2/SmokeChecklist.md`
- `Docs/Validation/BlueprintRound2/SmokeChecklist.zh-CN.md`

## 已接受运行时证据

- 宿主项目：`G:\UEProjects\MyProject`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254`
- 汇总文件：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound2\20260423_171254\summary.json`
- 本次宿主会话中的 runtime registered count：`102`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/BlueprintRound2`

已接受资产：

- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Clean_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Broken_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BPI_R2_Interface_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_InterfaceTarget_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Logic_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Toggle_20260423_171254`
- `/Game/UEBridgeMCPValidation/BlueprintRound2/BP_R2_Interaction_20260423_171254`

## 验证摘要

- `analyze-blueprint-compile-results` 在干净 Blueprint 上返回 compile success 且 issues 为 0。
- broken Blueprint 返回 1 条 compile diagnostic 和多条结构性 finding。
- `apply-blueprint-fixups(dry_run=true)` 返回 `status=dry_run`，且没有 modified assets。
- `refresh_all_nodes` 后执行 `remove_orphan_pins` 移除了 `6` 个 orphan pins。
- `conform_implemented_interfaces` 创建了 `Round2InterfaceCheck` 并清除了 `missing_interface_graph`。
- 3 个精选 Actor pattern 都成功编译并保存。
- 对已存在 pattern 资产路径再次创建返回 `UEBMCP_ASSET_ALREADY_EXISTS`。
- `auto-fix-blueprint-compile-errors` 仍能以 dry-run 接受原有 4 个 strategy。

## 已知边界

- `apply-blueprint-fixups` 只做安全结构性修复，不猜测缺失连线或默认值。
- `create-blueprint-pattern` v1 只支持 Actor Blueprint pattern。
- `analyze-blueprint-compile-results` 允许内存中编译，但不会主动保存资产。
