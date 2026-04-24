# Step 6 Issues

This file tracks validation findings discovered while closing Step 6 in `G:\UEProjects\MyProject`.

## Open issues

| Severity | Blocking | Area | Summary | Evidence | Suggested action |
| --- | --- | --- | --- | --- | --- |
| Non-blocker | No | Animation | `edit-anim-blueprint-state-machine` positive-path smoke was not exercised because the current validation AnimBP fixture has `state_machine_count=0`. | Final accepted run `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727\scenario-results.json` contains `uebridge.animation_query_blueprint` with `state_machine_count=0`. A targeted probe against `/MoverExamples/.../ABP_Manny` and `/MoverExamples/.../ABP_MannyExtended` returned `UEBMCP_ASSET_NOT_FOUND` because those plugin assets are not mounted in the current `MyProject` configuration. | Add a mounted AnimBP fixture with at least one state machine to `MyProject`, or explicitly enable a safe fixture source plugin for validation-only coverage. |

## Resolved blockers

| Severity | Blocking | Area | Summary | Resolution |
| --- | --- | --- | --- | --- |
| Blocker | Was blocking | Registry / shutdown | Optional tool modules could crash while unregistering tools during editor shutdown because stale tool-instance handles were dereferenced. | Fixed by changing the registry's lazy instance cache to `TWeakObjectPtr` and cleaning stale entries before reuse or unregister. Final accepted run exited cleanly and logged safe unregistration for `generate-external-content`, `generate-pcg-scatter`, and `edit-control-rig-graph`. |
| Blocker | Was blocking | External AI | The bad-config timeout path could crash after `generate-external-content` returned, because the HTTP completion callback captured stack state that no longer existed. | Fixed by moving provider request state to shared heap storage. Final accepted run returned structured `UEBMCP_EXTERNAL_AI_TIMEOUT` and then completed `preset_delete` and editor shutdown successfully. |

## Environment deviations

| Severity | Blocking | Area | Summary | Evidence | Suggested action |
| --- | --- | --- | --- | --- | --- |
| Note | No | Runtime environment | Final validation executed `UEBridgeMCP` on temporary port `18080` because an unrelated `LyraStarterGame` editor session was already listening on `127.0.0.1:8080`. | Final accepted run is recorded under `G:\UEProjects\UEBridgeMCP\Tmp\Validation\Step6\20260423_114727`; `G:\UEProjects\MyProject\Config\DefaultUEBridgeMCP.ini` was restored to `8080` after the run. | No product change required. Keep `8080` as the project default and continue to use a temporary override only when another local editor session is occupying that port. |

## Exit status

Current Step 6 validation status for `MyProject`:

- blocker issues: `0`
- non-blocking open issues: `1`
- final accepted run: `20260423_114727`

Practical conclusion:

- Step 6 is blocker-free and validation-ready in the current host project.
- One explicit fixture-coverage gap remains for Anim Blueprint state-machine positive-path smoke.
