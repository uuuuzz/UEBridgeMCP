# Blueprint Round 1 Smoke Checklist

## Goal

Validate Blueprint Phase 1A + 1B in `G:\UEProjects\MyProject` without introducing a separate smoke project or a new automated harness.

## Host And Asset Scope

- Host project: `G:\UEProjects\MyProject`
- MCP endpoint: `http://127.0.0.1:8080/mcp`
- Validation asset root: `/Game/UEBridgeMCPValidation/BlueprintRound1`
- Runtime-created Blueprint asset pattern: `/Game/UEBridgeMCPValidation/BlueprintRound1/BP_BlueprintRound1_<timestamp>`
- Runtime-created Blueprint Interface asset pattern: `/Game/UEBridgeMCPValidation/BlueprintRound1/BPI_BlueprintRound1_<timestamp>`
- Optional evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound1\<timestamp>`

## Preconditions

- `MyProject` editor is running with `UEBridgeMCP` auto-start enabled.
- `tools/list` and `initialize.capabilities.tools.registeredCount` are readable.
- No new dedicated validation harness is required for this round; manual MCP request/response capture is acceptable.

## Protocol And Visibility

- [ ] `initialize` succeeds against `http://127.0.0.1:8080/mcp`
- [ ] `tools/list` succeeds
- [ ] `tools/list` contains:
  - [ ] `create-blueprint-function`
  - [ ] `create-blueprint-event`
  - [ ] `edit-blueprint-function-signature`
  - [ ] `manage-blueprint-interfaces`
  - [ ] `layout-blueprint-graph`
  - [ ] `query-blueprint-findings`
- [ ] `tools/list` still contains `edit-blueprint-members` and `edit-blueprint-graph`

## Asset Setup

- [ ] Create `/Game/UEBridgeMCPValidation/BlueprintRound1/BPI_BlueprintRound1_<timestamp>` through `create-asset(asset_class="BlueprintInterface")`
- [ ] Create `/Game/UEBridgeMCPValidation/BlueprintRound1/BP_BlueprintRound1_<timestamp>` through `create-asset(asset_class="Blueprint")`
- [ ] Add at least one interface function to the runtime-created BPI before `add_interface`; use a non-void signature (for example a bool output) so `sync_graphs` can be validated through actual interface function graphs
- [ ] Base class is `/Script/Engine.Actor`
- [ ] Asset can be compiled and saved before Round 1 mutations begin

## Member-Layer Validation

- [ ] `edit-blueprint-members` creates at least one Blueprint variable
- [ ] `edit-blueprint-members` creates at least one function
- [ ] `edit-blueprint-members` creates at least one event dispatcher
- [ ] `create-blueprint-function` creates a new function graph with declared inputs and outputs
- [ ] `edit-blueprint-function-signature` updates category, tooltip, metadata, or pin defaults and those values read back correctly
- [ ] Local variable actions are exercised inside a function:
  - [ ] `create_local_variable`
  - [ ] `rename_local_variable`
  - [ ] `set_local_variable_properties`
  - [ ] `delete_local_variable`
- [ ] At least one `dry_run=true` member edit succeeds without persisting changes
- [ ] At least one structured failure path is exercised with `rollback_on_error=true`

## Interface Validation

- [ ] `manage-blueprint-interfaces` or `edit-blueprint-members` adds the runtime-created interface using `interface_path`
- [ ] Add-interface result includes `implemented_interfaces`
- [ ] Add-interface result includes `touched_graphs`
- [ ] Add-interface path is exercised without explicitly sending `sync_graphs`, so the default behavior is what gets validated
- [ ] Interface removal path is exercised
- [ ] `sync_graphs=true` default behavior is confirmed
- [ ] At least one structured failure path is captured for a missing or non-interface `interface_path`

## Graph-Layer Validation

- [ ] `create-blueprint-event` creates a custom event in `EventGraph`
- [ ] `edit-blueprint-graph` creates at least these Blueprint-specific operations successfully:
  - [ ] `add_branch`
  - [ ] `add_sequence`
  - [ ] `add_call_function`
  - [ ] `add_get_variable`
  - [ ] `add_set_variable`
  - [ ] `add_reroute`
  - [ ] `add_comment`
  - [ ] `comment_region`
  - [ ] `move_nodes_batch`
  - [ ] `layout_graph`
- [ ] `auto_connect` succeeds for at least one node pair or small node set
- [ ] Graph edits can still compile and save on the same Blueprint

## Findings Validation

- [ ] Introduce at least one structural issue in the validation Blueprint
- [ ] `query-blueprint-findings` reports one or more expected codes:
  - [ ] `orphan_pin`
  - [ ] `broken_or_deprecated_reference`
  - [ ] `unlinked_required_pin`
  - [ ] `missing_default_value`
  - [ ] `unresolved_member_reference`
  - [ ] `missing_interface_graph`
- [ ] Fix the issue
- [ ] Re-run `query-blueprint-findings` and confirm the corresponding finding disappears

## Compile, Save, And Exit Criteria

- [ ] `compile-assets` or in-tool compile path succeeds for the final Blueprint state
- [ ] Final asset saves successfully
- [ ] Final Blueprint remains loadable in `MyProject`
- [ ] No new validation harness was added solely for this round

## Evidence To Capture

- Accepted `initialize` response snippet
- Accepted `tools/list` snippet showing the six new tools
- Validation Blueprint and Blueprint Interface asset paths plus final compile/save result
- One before/after `query-blueprint-findings` sample
- One positive add/remove-interface request-response pair and one negative-path request-response pair
