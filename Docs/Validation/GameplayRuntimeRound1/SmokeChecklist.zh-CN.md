# Gameplay Runtime Round 1 Smoke Checklist

范围：Gameplay Runtime Phase 3C v1。本轮只覆盖只读运行时 Actor 状态、实时 AbilitySystemComponent 状态，以及 editor / PIE world 中的只读 collision trace。

宿主项目：`G:\UEProjects\MyProject`

验证资产根目录：`/Game/UEBridgeMCPValidation/GameplayRuntimeRound1`

证据根目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\GameplayRuntimeRound1\<timestamp>`

## Checklist

- 确认 `tools/list` 暴露 `query-runtime-actor-state`、`query-ability-system-state`、`trace-gameplay-collision`。
- 在验证目录下创建临时 Actor Blueprint。
- 使用 `manage-ability-system-bindings` 给该 Actor Blueprint 添加 `AbilitySystemComponent`，并 compile/save。
- 运行时放置一个带 blocking collision 的临时 StaticMeshActor，以及一个带 ASC 的临时 Actor Blueprint 实例。
- 通过 `pie-session` 启动 PIE，并轮询 `get-state` 直到 `state=running`。
- 对 PIE StaticMeshActor 调用 `query-runtime-actor-state`，确认 world type 是 `PIE`、component count 非 0，且包含 collision 明细。
- 对 PIE ASC Actor 调用 `query-ability-system-state`，确认 ability system payload 有效并返回 component name。
- 在 PIE world 调用 `trace-gameplay-collision`，确认返回 blocking hit 和结构化 hit actor/component 信息。
- 对非 GAS StaticMeshActor 调用 `query-ability-system-state`，确认返回结构化 `UEBMCP_ABILITY_SYSTEM_NOT_FOUND`。
- 停止 PIE，轮询到 `state=not_running`，然后清理临时 editor-world Actor。

## Latest Passing Run

- Timestamp: `20260423_211350`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\GameplayRuntimeRound1\20260423_211350`
- Asset: `/Game/UEBridgeMCPValidation/GameplayRuntimeRound1/BP_GameplayRuntimePIE_20260423_211350`
- Runtime labels:
- `UEBridgeMCP_RuntimePIE_20260423_211350`
- `UEBridgeMCP_RuntimePIEGAS_20260423_211350`

## Notes

- 最终验收 run 使用 PIE world；同证据根目录下保留了一轮 editor-world-only 探索 run，便于排查。
- `query-ability-system-state` 是只读工具，不负责 grant ability 或修改 runtime tags。
- `trace-gameplay-collision` 是只读 trace，不推进仿真。
