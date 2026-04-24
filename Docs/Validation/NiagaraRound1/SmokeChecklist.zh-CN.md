# Niagara Round 1 Smoke Checklist

范围：

- 只覆盖 Niagara Phase 2A。
- 不验证 Audio、MetaSound、任意 Niagara graph 深度编辑，也不验证完整 module stack authoring。
- 宿主项目固定为 `G:\UEProjects\MyProject`。
- 验证资产固定放在 `/Game/UEBridgeMCPValidation/NiagaraRound1`。
- 证据目录固定放在 `G:\UEProjects\UEBridgeMCP\Tmp\Validation\NiagaraRound1\<timestamp>`。

Checklist：

- 当 Niagara 可用时，`tools/list` 能看到 5 个 Niagara Phase 2A 条件工具。
- `get-project-info.optional_capabilities.niagara_available` 在启用 Niagara 的宿主里为 `true`。
- `create-niagara-system-from-template` 能创建带时间戳的新 Niagara system，并可 compile/save。
- `query-niagara-system-summary` 返回 system readiness、emitter count、user parameter count 和 user parameter 明细。
- `edit-niagara-user-parameters` 覆盖 `dry_run=true`、`add_parameter`、`set_default`、`rename_parameter`、`remove_parameter`、final compile、save，以及至少一条结构化失败。
- `query-niagara-emitter-summary` 对空/default system 覆盖 no-emitter 结构化失败；如果宿主有带 emitter 的 template，则优先覆盖正向 emitter-handle summary。
- `apply-niagara-system-to-actor` 能在 editor-world Actor 上创建或更新 NiagaraComponent，至少应用一条 override，并保存或报告 modified world。
- 在 editor-world smoke 中，`activate_now=true` 应返回 deferred warning，而不是直接在 editor GameThread 上启动 Niagara simulation。
- 回归检查：启用 Niagara 条件注册后，已有 Blueprint Round 1/2 工具仍然可见。

证据要求：

- 保留 tool visibility、project info、create、query、parameter edit、actor apply 和负路径的原始 MCP 响应。
- 保留一个紧凑的 `summary.json`，记录创建资产路径、Actor/Component 名称、成功标记、warning 数量和结构化错误码。
