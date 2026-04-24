# Audio Round 1 Smoke Checklist

范围：

- 只覆盖 Audio Phase 2B。
- 不验证 MetaSound、audio mixer runtime profiling、SoundClass graph authoring，也不做超出 v1 支持操作的任意 SoundCue graph 深度编辑。
- 宿主项目固定为 `G:\UEProjects\MyProject`。
- 验证资产固定放在 `/Game/UEBridgeMCPValidation/AudioRound1`。
- 证据目录固定放在 `G:\UEProjects\UEBridgeMCP\Tmp\Validation\AudioRound1\<timestamp>`。

Checklist：

- `tools/list` 能看到 5 个 Audio Phase 2B 工具：`query-audio-asset-summary`、`create-sound-cue`、`edit-sound-cue-routing`、`create-audio-component-setup`、`apply-audio-to-actor`。
- `query-audio-asset-summary` 能对引擎 fixture `/Engine/EngineSounds/WhiteNoise.WhiteNoise` 返回有效 `SoundWave` 摘要。
- `create-sound-cue` 能在 `/Game/UEBridgeMCPValidation/AudioRound1` 下创建带时间戳的 SoundCue，使用 `initial_sound_wave_path` 初始化，并保存资产。
- `query-audio-asset-summary` 能返回新建 SoundCue 摘要和根节点路由结构。
- `edit-sound-cue-routing` 覆盖 `dry_run=true`、`set_random_waves`、`wrap_with_attenuation`、`set_volume_multiplier`、save，以及至少一条带 `rollback_on_error=true` 的结构化失败路径。
- `create-audio-component-setup` 覆盖 dry-run 和 editor-world Actor 正向设置路径，smoke 中使用 `save=false`。
- `apply-audio-to-actor` 覆盖 dry-run 和 editor-world 正向应用路径；`play_now=true` 应返回 `playback_deferred=true`，而不是直接在 editor GameThread 上播放。
- 至少一条 actor/audio 负路径返回结构化错误，例如缺失 SoundCue 路径。
- smoke 中添加的临时 Actor component 应清理，或保持未保存状态。

证据要求：

- 保留 initialize、tool visibility、SoundWave query、SoundCue create/query/edit、Actor component setup/apply 和负路径的原始 MCP 响应。
- 保留紧凑的 `summary.json`，记录创建资产路径、目标 Actor/Component 名称、成功标志、播放延迟状态和结构化错误校验。
