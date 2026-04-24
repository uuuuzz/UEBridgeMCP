# Macro / Utility Round 1 Provenance

日期：2026-04-23

阶段：Macro / Utility Phase 4C v1

宿主工程：`G:\UEProjects\MyProject`

实现仓库：`G:\UEProjects\UEBridgeMCP`

## Implementation

- 在核心 editor module 中新增 5 个 always-on macro / utility 工具。
- `query-workspace-health` 返回 project、plugin、server、world、path、capability、dirty-package 状态。
- `run-project-maintenance-checks` 组合 workspace health、保守 unused asset 检查和可选 Blueprint compile check。
- `generate-blueprint-pattern` 包装 `create-blueprint-pattern`，并增加 dry-run delegated plan。
- `generate-level-pattern` 创建 curated engine-only editor-world scaffold patterns。
- `run-editor-macro` 只暴露小型 curated macro catalog，明确不提供任意脚本执行入口。

## Verification

- Build: `MyProjectEditor Win64 Development` 使用 `-MaxParallelActions=4` 构建成功。
- `tools/list` runtime tool count: `145`。
- 最终证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\MacroUtilityRound1\20260423_222221`
- 运行时创建的验证 Blueprint：
- `/Game/UEBridgeMCPValidation/MacroUtilityRound1/BP_GeneratedPattern_20260423_222221`
- 临时 editor-world actor prefix：
- `UEBMCP_MacroUtility_20260423_222221`

## Regressions Covered

- 5 个 Phase 4C 工具全部在 `tools/list` 可见。
- Workspace health 返回 project、server、world、optional capability 和 dirty package 状态。
- Maintenance checks 覆盖 dry-run check bundle 与真实 Blueprint compile check。
- Blueprint pattern generation 的 dry-run 和真实 create/compile/save 路径通过。
- Level pattern generation 的 dry-run 和真实 editor-world spawn 路径通过。
- Editor macro 的 health、compile dry-run、compile real、cleanup dry-run、cleanup real 路径通过。
- Unsupported editor macro 与 unsupported level pattern 均返回结构化 `UEBMCP_INVALID_ACTION`。

## Boundaries

- 本轮不创建万能 macro / script executor。
- 本轮不新增 C++ / config / source 任意全文索引。
- 本轮不替代 `run-workflow-preset`，只补 curated utility entrypoints。
- 本轮的 level pattern 不引入项目内容依赖。
