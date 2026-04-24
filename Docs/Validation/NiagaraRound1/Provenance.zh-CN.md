# Niagara Round 1 Provenance

目的：

- 记录 Niagara Phase 2A 的首次实现与 smoke 证据。
- 明确边界：本轮不进入 Audio、MetaSound，也不做任意 Niagara graph 深度编辑。

实现来源：

- Niagara 工具是核心编辑器模块中的条件工具，只在 `FMcpOptionalCapabilityUtils::IsNiagaraAvailable()` 为 true 时注册。
- 核心模块新增依赖 `Niagara` 和 `NiagaraEditor`，插件描述文件把 Niagara 声明为 optional plugin dependency。
- 共享 Niagara 行为集中在 `Source/UEBridgeMCPEditor/Private/Tools/Niagara/NiagaraToolUtils.*`。
- 工具入口为 `query-niagara-system-summary`、`query-niagara-emitter-summary`、`create-niagara-system-from-template`、`edit-niagara-user-parameters`、`apply-niagara-system-to-actor`。

验证来源：

- 宿主：`G:\UEProjects\MyProject`。
- 资产根目录：`/Game/UEBridgeMCPValidation/NiagaraRound1`。
- 证据根目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\NiagaraRound1`。
- 运行时创建资产使用带时间戳的名字，例如 `NS_NiagaraRound1_<timestamp>`；测试 Actor 使用类似 `NiagaraRound1Actor_<timestamp>` 的 label。

v1 边界：

- User parameter 编辑支持 bool、int32、float、vector2、vector3、position、vector4 和 color。
- Emitter 摘要是浅层摘要，不编辑 emitter stack。
- `create-niagara-system-from-template` 在未传 template 时创建 empty/default system；更丰富的 template catalog 留给后续。
- `apply-niagara-system-to-actor` 面向 editor-world actor component，不创建 transient runtime effect。
- editor world 中的即时激活会被 deferred，避免阻塞 editor GameThread；PIE/runtime 激活留给后续单独验证。
