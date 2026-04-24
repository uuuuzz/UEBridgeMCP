# Sequencer Round 1 Provenance

本轮源于后续作者工具路线图里的一个缺口：Sequencer 已有最小写工具，但还没有“创建资产、编辑、查询、验证”的自包含闭环。

实现变更：

- `create-asset` 现在支持 `asset_class="LevelSequence"`，并通过 `ULevelSequence::Initialize()` 初始化资产，确保后续编辑前已有真实 MovieScene。
- 新增条件 Sequencer 查询工具 `query-level-sequence-summary`，覆盖 playback range、frame rate、binding、possessable、spawnable、track、section 和 Camera Cut 明细。
- `edit-sequencer-tracks` 现在通过引擎 `AddCameraCutTrack(...)` 路径创建 Camera Cut，因此 `GetCameraCutTrack()` 会正确指向专用轨道。
- `query-level-sequence-summary` 仍保留兼容 fallback，用于识别旧工具构建曾经误创建成普通 master track 的 Camera Cut。
- Binding display name 写入不再使用 deprecated 的 `FMovieSceneBinding::SetName`，改为写入 possessable / spawnable 条目。

验证运行：

- Timestamp：`20260423_225804`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\SequencerRound1\20260423_225804`
- 创建资产：`/Game/UEBridgeMCPValidation/SequencerRound1/LS_SequencerRound1_20260423_225804`
- 绑定 Actor：`SM_SkySphere`

构建：

- 命令：`Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- 结果：成功。

范围边界：

- 本轮只覆盖 Level Sequence 资产创建、摘要查询、Actor binding、基础 track/key、playback range 和 Camera Cut 创建。
- 不扩展到 shot / sequence hierarchy authoring、spawnable 创建、cinematic camera 创建、Control Rig sequencing、audio tracks、render queue 集成或 timeline UI 自动化。
