# Animation Round 2 Provenance

This round was added after the Step 6 report identified one remaining non-blocking validation gap: `edit-anim-blueprint-state-machine` had not been positively smoke-tested because the available host AnimBP fixture had no state machine.

Implementation changes:

- `create-asset` now checks specialized Blueprint asset classes before the generic `UBlueprint` branch, preventing `AnimBlueprint` from being accidentally created as an Actor Blueprint.
- `create-asset(asset_class="AnimBlueprint")` now uses the engine `UAnimBlueprintFactory` with a Skeleton asset path supplied through `parent_class`.
- `edit-anim-blueprint-state-machine` now supports `create_state_machine` and `ensure_state_machine`, with optional `connect_to_output`, so a state-machine fixture can be created entirely through public MCP tools.

Validation run:

- Timestamp: `20260423_223834`
- Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\AnimationRound2\20260423_223834`
- Created asset: `/Game/UEBridgeMCPValidation/AnimationRound2/ABP_StateMachine_20260423_223834`
- Skeleton source: `/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton.TutorialTPP_Skeleton`
- Sequence source: `/Game/UEBridgeMCPValidation/Step6/Animations/VLD_Tutorial_Idle.VLD_Tutorial_Idle`

Build:

- Command: `Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- Result: succeeded.

Scope boundaries:

- This round only closes the AnimBlueprint state-machine fixture and smoke gap.
- It does not expand into blend spaces, transition rule graph authoring, montage slot workflows, Control Rig, or runtime animation playback assertions.
