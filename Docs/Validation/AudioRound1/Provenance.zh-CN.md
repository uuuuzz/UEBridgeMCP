# Audio Round 1 Provenance

目的：

- 记录 Audio Phase 2B 的首次实现与 smoke 证据。
- 明确边界：本轮不进入 MetaSound、SoundClass graph authoring，也不做任意 SoundCue graph 深度编辑。

实现来源：

- Audio 工具是核心编辑器 always-on 工具，直接从 `RegisterBuiltInTools()` 注册。
- 编辑器模块新增 `AudioEditor` 依赖，用于通过引擎 SoundCue factory 创建 SoundCue。
- 共享 Audio 行为集中在 `Source/UEBridgeMCPEditor/Private/Tools/Audio/AudioToolUtils.*`。
- 工具入口为 `query-audio-asset-summary`、`create-sound-cue`、`edit-sound-cue-routing`、`create-audio-component-setup`、`apply-audio-to-actor`。
- editor-world 的 `play_now` 会被主动延迟，避免 smoke 中直接在 editor GameThread 上启动音频播放。

验证来源：

- 宿主：`G:\UEProjects\MyProject`。
- 资产根目录：`/Game/UEBridgeMCPValidation/AudioRound1`。
- 证据根目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\AudioRound1`。
- 本轮最终 smoke 证据：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\AudioRound1\20260423_185956`。
- 运行时创建的 SoundCue 使用带时间戳的名字，例如 `SC_AudioRound1_<timestamp>`。
- 最终 smoke 使用现有 editor-world Actor `SM_SkySphere`，临时组件命名为 `AudioRound1Component_<timestamp>`，并在结束后通过 `run-python-script` 清理。

v1 边界：

- SoundCue routing v1 支持 wave-player、random、mixer、attenuation wrapping、multiplier 编辑、attenuation asset 绑定、override flag 编辑和 clear routing。
- SoundCue graph layout 与任意底层节点图编辑仍不在本轮范围内。
- AudioComponent 工具聚焦 editor-world 组件设置与应用；runtime/PIE 播放验证留给后续。
- MetaSound 已由单独的 MetaSound Round 1 / Phase 2C 验证覆盖。
- 最终 Audio smoke 没有使用 `edit-level-batch` 的 actor spawning，因为前序验证暴露了 editor-world `spawn_actor` 卡住问题；最终断言使用公开 Audio 工具和一个最小 cleanup 脚本完成。
