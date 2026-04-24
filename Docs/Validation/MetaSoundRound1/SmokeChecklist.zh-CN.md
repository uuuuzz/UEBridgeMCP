# MetaSound Round 1 Smoke Checklist

范围：

- 只覆盖 MetaSound Phase 2C。
- 不验证 Audio、Niagara、运行时播放、任意 MetaSound 图生成，或完整 DSP 图作者能力。
- 宿主项目固定为 `G:\UEProjects\MyProject`。
- 验证资产放在 `/Game/UEBridgeMCPValidation/MetaSoundRound1`。
- 证据目录放在 `G:\UEProjects\UEBridgeMCP\Tmp\Validation\MetaSoundRound1\<timestamp>`。

Checklist：

- 当 MetaSound 模块可用时，`tools/list` 能看到 4 个 MetaSound Phase 2C 条件工具：`query-metasound-summary`、`create-metasound-source`、`edit-metasound-graph`、`set-metasound-input-defaults`。
- 在启用 MetaSound 的宿主中，`get-project-info.optional_capabilities.metasound_available` 为 `true`。
- `create-metasound-source` 能在 `/Game/UEBridgeMCPValidation/MetaSoundRound1` 下创建带时间戳的 MetaSound Source，设置 output format，写入受支持的 v1 graph input，并保存资产。
- `query-metasound-summary` 能返回创建后的 MetaSound Source 摘要，包括 input/output 数量、interface 数量、node 数量，以及可选 graph nodes/edges。
- `set-metasound-input-defaults` 覆盖 `dry_run=true`、bool/float 默认值正向修改、rollback 和 save 路径。
- `edit-metasound-graph` 覆盖 `dry_run=true`、graph input 正向创建、`layout_graph`、rollback 和 save 路径。
- 至少一条负路径返回结构化错误，例如给不存在的 graph input 设置默认值。
- 所有编辑后，创建的 MetaSound Source 仍可查询，并返回符合预期的 input 数量增长。

证据要求：

- 保存 tool visibility、project info、source 创建、初始/最终摘要、default dry-run/apply、graph edit dry-run/apply 和负路径的原始 MCP 响应。
- 保存一个紧凑的 `summary.json`，包含创建资产路径、工具可见性、optional capability flag、成功标记、最终图统计和结构化错误码。
