# Release Preflight

Use this before tagging or packaging a release. It verifies the runtime inventory and the compatibility alias layer without relying on a hard-coded tool count.

## Run

Start the Unreal Editor with UEBridgeMCP enabled, then run from the plugin root:

```powershell
powershell -ExecutionPolicy Bypass -File Validation\Smoke\Invoke-ReleasePreflight.ps1
```

If the editor is not running and you only want static validation:

```powershell
powershell -ExecutionPolicy Bypass -File Validation\Smoke\Invoke-ReleasePreflight.ps1 -AllowOffline
```

The script writes evidence to:

```text
Tmp/Validation/ReleasePreflight/<timestamp>/summary.json
```

## Gates

The release should pass these checks:

- Static compatibility alias validation passes.
- `initialize.capabilities.tools.registeredCount` equals `tools/list.length`.
- `resources/list` and `prompts/list` return successfully.
- Expected compatibility aliases are visible in `tools/list`.
- Read-only or dry-run alias calls complete.
- Safety probes confirm high-risk operations reject unconfirmed execution.

## Safety Expectations

High-risk tools now require an explicit signal before mutating editor state:

- `build-and-relaunch`: use `dry_run=true` for planning, and `confirm_shutdown=true` for real execution.
- `run-python-script`: use `dry_run=true` for planning, and `confirm_execution=true` for real execution.
- `manage-assets`: non-dry-run `delete` requires `confirm_delete=true`; `consolidate` requires `confirm_consolidate=true`.
- `source-control-assets`: supports root-level `dry_run`; non-dry-run `revert`, `submit`, and `sync` require `confirm_write=true`.

These confirmations are intentionally simple booleans so MCP clients can set them only after the assistant has made the destructive intent explicit.
