# Animation Round 2 Provenance

本轮源于 Step 6 报告里的一个剩余非阻塞验证缺口：`edit-anim-blueprint-state-machine` 缺少正向 smoke，因为当时可用的宿主 AnimBP fixture 没有 state machine。

实现变更：

- `create-asset` 现在先判断专用 Blueprint 资产类型，再落到通用 `UBlueprint` 分支，避免 `AnimBlueprint` 被误创建成 Actor Blueprint。
- `create-asset(asset_class="AnimBlueprint")` 现在使用引擎 `UAnimBlueprintFactory`，并通过 `parent_class` 传入 Skeleton 资产路径。
- `edit-anim-blueprint-state-machine` 现在支持 `create_state_machine` 与 `ensure_state_machine`，并支持可选 `connect_to_output`，因此可以完全通过公开 MCP 工具构造 state-machine fixture。

验证运行：

- Timestamp：`20260423_223834`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\AnimationRound2\20260423_223834`
- 创建资产：`/Game/UEBridgeMCPValidation/AnimationRound2/ABP_StateMachine_20260423_223834`
- Skeleton 来源：`/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton`
- Sequence 来源：`/Game/UEBridgeMCPValidation/Step6/Animations/VLD_Tutorial_Idle.VLD_Tutorial_Idle`

构建：

- 命令：`Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- 结果：成功。

范围边界：

- 本轮只关闭 AnimBlueprint state-machine fixture 与 smoke 缺口。
- 不扩展到 BlendSpace、transition rule graph authoring、montage slot workflow、Control Rig 或 runtime animation playback assertions。
