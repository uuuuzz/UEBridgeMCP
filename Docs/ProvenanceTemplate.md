# UEBridgeMCP Provenance Template

Use this note in PR descriptions, release notes, or internal implementation records whenever work is inspired by another plugin, product, or benchmark.

```md
## Provenance

- Capability family:
- Roadmap step:
- Request / issue / milestone:

### Inputs consulted
- Product judgment:
- Black-box usage or screenshots:
- Public docs / README / marketing pages:
- Unreal or MCP standard references:

### Explicitly not used
- No source code was copied.
- No private helper structure, file layout, or registration order was copied.
- No prompt text, schema prose, error wording, screenshots, or marketing copy was copied.

### Independent design choices
- UEBridgeMCP tool names chosen:
- Why the names fit existing UEBridgeMCP style:
- Why the API uses `query-*` / `edit-*` / `create-*` / `manage-*` / `generate-*` naming:
- Batch / transaction / `dry_run` / `save` / `rollback_on_error` decisions:
- Intentional differences from the benchmarked product:

### Validation
- Build / compile verification:
- Manual smoke test:
- Known gaps / next step:
```

Short guidance:

- Shared terms are acceptable when they are standard Unreal or MCP vocabulary.
- Anything that reads like authored prose should be rewritten in UEBridgeMCP's own voice.
- When in doubt, describe the user goal and your independent design, not the other product's implementation.
