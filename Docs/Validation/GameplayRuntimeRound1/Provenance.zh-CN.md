# Gameplay Runtime Round 1 Provenance

日期：2026-04-23

阶段：Gameplay Runtime Phase 3C v1

宿主项目：`G:\UEProjects\MyProject`

实现仓库：`G:\UEProjects\UEBridgeMCP`

## Implementation

- 在核心编辑器模块新增 3 个 always-on runtime gameplay 查询工具：
- `query-runtime-actor-state`
- `query-ability-system-state`
- `trace-gameplay-collision`
- 新增共享 helper：`Source/UEBridgeMCPEditor/Private/Tools/Gameplay/RuntimeGameplayToolUtils.*`，负责 section filter、vector parsing、world / actor runtime serialization、hit serialization 和 live GAS state serialization。
- 在 `RegisterBuiltInTools()` 中和现有 PIE 查询 / 断言工具一起注册。
- 复用现有 actor resolution、physics collision channel parsing、actor handle 和 GAS 模块依赖。

## Verification

- Build：`MyProjectEditor Win64 Development` 使用 `-MaxParallelActions=4` 构建成功。
- `tools/list` 运行时工具数：`131`。
- 最终证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\GameplayRuntimeRound1\20260423_211350`
- 运行时创建的验证资产：
- `/Game/UEBridgeMCPValidation/GameplayRuntimeRound1/BP_GameplayRuntimePIE_20260423_211350`
- PIE 验证标签：
- `UEBridgeMCP_RuntimePIE_20260423_211350`
- `UEBridgeMCP_RuntimePIEGAS_20260423_211350`

## Regressions Covered

- `tools/list` 能看到 3 个 Gameplay Runtime Phase 3C 工具。
- `create-asset` 和 `manage-ability-system-bindings` 仍能创建带 `AbilitySystemComponent` 的 Actor Blueprint。
- `pie-session` 可以启动、进入 `running`、停止，并回到 `not_running`。
- `query-runtime-actor-state` 可以按 label 解析 PIE actor，并返回 transform/component/collision runtime state。
- `query-ability-system-state` 可以按 label 解析带 `AbilitySystemComponent` 的 PIE actor，并返回 live GAS state。
- `trace-gameplay-collision` 可以在 PIE world 执行 line trace，并返回结构化 blocking hit。
- 非 GAS actor 会返回结构化 `UEBMCP_ABILITY_SYSTEM_NOT_FOUND`。

## Boundaries

- 本轮不在运行时 grant 或 activate ability。
- 本轮不验证 prediction、replication behavior 或 GameplayAbility task graph。
- 本轮不做长时间物理仿真断言或 Chaos diagnostics。
- 除 smoke fixture setup / cleanup 外，本轮不修改运行时 Actor 状态。
