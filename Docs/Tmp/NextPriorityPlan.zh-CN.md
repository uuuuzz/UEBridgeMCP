# UEBridgeMCP 后续四个优先级开工方案

## 0. 当前定位

- `UEBridgeMCP` 的 Step 0 到 Step 6 主路线已经基本收口。
- Step 6 最终验收运行口径下，`UEBridgeMCP` 运行时是 `93` 个工具。
- 这意味着项目已经完成主线能力闭环，但长尾工具覆盖还可以继续扩展。
- 后续工作不建议再按“大而全”追数字，而是按高价值长尾能力族继续扩展。

本方案把后续追平工作拆成四个优先级，目标是让你可以直接按阶段开工，而不是重新做一轮大路线规划。

## 1. 总体原则

- 继续按“能力族”推进，不按单纯工具数量逐个堆叠。
- 命名继续使用 `query-* / inspect-* / edit-* / edit-*-batch / create-* / manage-* / generate-* / apply-*`。
- 新写接口继续优先使用 `operations[]` 或 `actions[]`，统一支持 `dry_run`、必要时支持 `save`、`rollback_on_error`。
- 能放在现有核心层的能力继续放在 `UEBridgeMCPEditor`；明显依赖可选插件的能力继续走条件注册或独立扩展模块。
- 所有对标都只对齐能力覆盖和黑盒体验，不复用第三方 schema、文案、错误文本或模块切分。
- 每个优先级结束时都要补齐：工具注册、双语文档、smoke checklist、provenance 说明。

## 2. 第一优先：Blueprint 长尾作者工具

### 2.1 为什么第一优先

- 这是当前最直接影响日常 AI 编图体验的一块。
- `UEBridgeMCP` 已经有 Blueprint 核心底座，但还没有把长尾作者能力补成完整工作流。
- 这块一旦补齐，后续 Gameplay、UI、AI、Animation 几条线都会更顺。

### 2.2 目标

把 Blueprint 能力从“能建核心结构”扩成“能完成大多数日常图编辑与脚本搭建任务”，覆盖函数、事件、接口、变量、常见控制流节点和图内辅助操作。

### 2.3 建议分三段开工

#### Phase 1A：成员与图骨架补齐

优先补这些能力：

- `edit-blueprint-members` 扩展：
  - local variable
  - function input/output 默认值
  - interface implementation
  - event dispatcher
- `edit-blueprint-graph` 扩展：
  - create custom event
  - create function graph
  - create macro graph
  - add reroute / comment / sequence / branch / switch / select / make-array / break-struct / make-struct
- 新增：
  - `create-blueprint-function`
  - `create-blueprint-event`
  - `edit-blueprint-function-signature`
  - `manage-blueprint-interfaces`

这一段的目标不是追所有节点，而是先把“图骨架”和“成员定义”补完整。

#### Phase 1B：常见节点作者工作流

优先补这些能力：

- 常见 K2 节点工厂：
  - call function
  - get/set variable
  - branch
  - sequence
  - for each
  - switch on enum
  - cast
  - spawn actor
  - create widget
- 图连线与整理增强：
  - pin 自动匹配
  - graph layout
  - node selection / batch move
  - graph comment region
- Blueprint 组件默认值与 construction script 入口增强

建议新增或扩展：

- `edit-blueprint-graph` 的 `operations[]` 再细化一轮
- `layout-blueprint-graph`
- `query-blueprint-findings`

#### Phase 1C：编译反馈与修复辅助

补这类能力：

- 编译错误按节点定位
- 未连接 pin、缺少默认值、失效引用等结构化 findings
- 常见错误自动修复建议
- Blueprint 工作流脚手架模板

建议新增：

- `analyze-blueprint-compile-results`
- `apply-blueprint-fixups`
- `create-blueprint-pattern`

### 2.4 验收标准

- 可以从 0 创建一个带变量、函数、事件、接口实现的测试 Blueprint。
- 可以通过批量图编辑把一个空蓝图扩成基础交互逻辑。
- 可以在不手动点编辑器的情况下完成常见节点创建、连线、布局和编译。
- 编译错误能够结构化回读，并能完成至少一类自动修复。

### 2.5 非目标

- 不追完整覆盖所有引擎节点工厂。
- 不追复杂 latent 节点调度和所有特殊 K2 schema 分支。
- 不追 1:1 对齐第三方 Blueprint 专用小工具数量。

## 3. 第二优先：Niagara / Audio / MetaSound

### 3.1 为什么第二优先

- 这是当前最明显的整块空白域之一。
- 补上之后，项目就不只是在“逻辑和编辑器作者”层面对齐，而是开始补足表现层生产力。
- 这块对游戏项目的实际价值很高，而且能明显提升内容生产覆盖。

### 3.2 目标

先把 VFX 和音频工作流补到 v1 可用，而不是一开始就追复杂 graph 深编辑。

### 3.3 建议分三段开工

#### Phase 2A：Niagara 读写基础

优先补这些能力：

- `query-niagara-system-summary`
- `query-niagara-emitter-summary`
- `create-niagara-system-from-template`
- `edit-niagara-user-parameters`
- `apply-niagara-system-to-actor`

第一版先聚焦：

- 系统/发射器摘要
- user parameter
- renderer 开关
- 模板复制与轻量配置

先不要追：

- 任意 Niagara graph 深度编辑
- 所有 module stack 改写

#### Phase 2B：Audio 资产工作流

优先补这些能力：

- `query-audio-asset-summary`
- `create-sound-cue`
- `edit-sound-cue-routing`
- `create-audio-component-setup`
- `apply-audio-to-actor`

覆盖范围建议先放在：

- SoundWave
- SoundCue
- AudioComponent
- 衰减、并发、基础路由

#### Phase 2C：MetaSound v1

优先补这些能力：

- `query-metasound-summary`
- `create-metasound-source`
- `edit-metasound-graph`
- `set-metasound-input-defaults`

第一版只做：

- 常见输入输出节点
- 参数默认值
- 基础连接
- graph layout

### 3.4 验收标准

- 能创建一个最小 Niagara 系统并应用到关卡 Actor。
- 能创建一个 SoundCue，并把它绑定到测试 Actor。
- 能创建一个最小 MetaSound Source，设置输入默认值，并回读摘要。
- Niagara、Audio、MetaSound 三条链路至少各完成一次正向 smoke。

### 3.5 非目标

- 不追所有 Niagara module stack 写操作。
- 不做音频混音台、录音、复杂 runtime profiling。
- 不追完整 MetaSound 深图编辑。

## 4. 第三优先：Physics + GAS + 更深的 Gameplay Runtime

### 4.1 为什么第三优先

- 这块决定 `UEBridgeMCP` 能不能从“编辑器作者工具”继续往“玩法系统自动化”走。
- 这类系统层能力仍然有明显扩展空间。
- 这块虽然复杂，但价值高，尤其适合在 Blueprint 长尾补完后继续推进。

### 4.2 目标

补齐 Physics 与 GAS 的最小生产工作流，同时让 Gameplay runtime 查询和配置更接近真实项目需求。

### 4.3 建议分三段开工

#### Phase 3A：Physics 配置与查询

优先补这些能力：

- `query-physics-summary`
- `edit-collision-settings`
- `edit-physics-simulation`
- `create-physics-constraint`
- `edit-physics-constraint`
- `apply-physical-material`

覆盖范围建议先放在：

- collision profile
- object channel / response
- simulate physics
- mass / gravity / damping
- physical material
- basic constraint

#### Phase 3B：GAS 资产与配置

优先补这些能力：

- `create-gameplay-ability`
- `create-gameplay-effect`
- `create-attribute-set`
- `query-gas-asset-summary`
- `edit-gameplay-effect-modifiers`
- `manage-ability-system-bindings`

第一版建议重点：

- 资产创建
- 常见 tag / cooldown / cost / modifier 配置
- AbilitySystemComponent 基础绑定

先不要追：

- 复杂 execution calculation
- prediction 深水区
- 所有 ability task 自动编图

#### Phase 3C：更深的 Gameplay Runtime

扩展这类能力：

- 更完整的 `query-gameplay-state`
- Actor/Component runtime 状态快照
- runtime collision / overlap / trace 结果查询
- replication 与 authority 状态细化

建议新增：

- `query-runtime-actor-state`
- `trace-gameplay-collision`
- `query-ability-system-state`

### 4.4 验收标准

- 能对测试 Actor 配置 collision、physics simulate 与物理材质，并回读。
- 能创建一组最小 GAS 资产并建立基础引用关系。
- 能查询到 AbilitySystem 或 runtime gameplay 的结构化状态。
- 至少完成一次 physics 正向 smoke 和一次 GAS 资产正向 smoke。

### 4.5 非目标

- 不追网络预测和 GAS 深度 runtime 驱动。
- 不做 Chaos 高阶 authoring。
- 不追所有物理诊断或 profiler。

## 5. 第四优先：Search / EngineAPI / Macro / Developer Utility

### 5.1 为什么第四优先

- 这块不是最核心的创作链路，但非常影响“好不好用”。
- 这类小而杂但高频的开发辅助，很影响工具面的实际体感。
- 这块适合在前三优先级补齐后，集中做一轮体验型增强。

### 5.2 目标

把项目级发现、检索、接口查询和复用型脚手架补成一层开发辅助面，减少“知道要做什么，但找不到入口”的成本。

### 5.3 建议分三段开工

#### Phase 4A：项目搜索增强

优先补这些能力：

- `search-project`
- `search-assets-advanced`
- `search-blueprint-symbols`
- `search-level-entities`
- `search-content-by-class`

能力重点：

- 模糊匹配
- CamelCase 片段匹配
- 排序分数
- 路径过滤
- 引用域内搜索

#### Phase 4B：Engine API / 文档辅助

优先补这些能力：

- `query-engine-api-symbol`
- `query-class-member-summary`
- `query-plugin-capabilities`
- `query-editor-subsystem-summary`

这层重点不是联网查文档，而是利用本地引擎反射、类层级、插件加载状态和编辑器子系统信息做辅助解释。

#### Phase 4C：Macro / Utility / 高层工作流脚手架

优先补这些能力：

- `run-editor-macro`
- `generate-blueprint-pattern`
- `generate-level-pattern`
- `run-project-maintenance-checks`
- `query-workspace-health`

适合放进来的内容：

- 常见批处理组合
- 目录治理脚手架
- Blueprint / Level 预制流程
- 维护类检查工具

### 5.4 验收标准

- 能按模糊关键词找到资产、蓝图成员或关卡实体。
- 能按 class、path、symbol 做结构化搜索。
- 能返回本地引擎 API 或 editor subsystem 的结构化摘要。
- 至少有一类 macro / utility 能减少多次重复工具调用。

### 5.5 非目标

- 不做外部搜索引擎代理。
- 不做在线文档镜像。
- 不做“万能脚本执行器”式的危险入口。

## 6. 推荐推进顺序

建议严格按下面顺序推进：

1. 第一优先：Blueprint 长尾作者工具
2. 第二优先：Niagara / Audio / MetaSound
3. 第三优先：Physics + GAS + 更深的 Gameplay Runtime
4. 第四优先：Search / EngineAPI / Macro / Developer Utility

原因很简单：

- 第一优先最直接影响所有后续工具面的作者效率。
- 第二优先能最快补齐表现层空白。
- 第三优先复杂度更高，适合放在基础作者能力增强之后。
- 第四优先偏体验增强，价值高，但对主生产链路的阻塞性最低。

## 7. 建议的执行方式

如果按“可以直接开工”的节奏来走，建议这样拆：

### 第一轮

- 只做第一优先的 `Phase 1A + Phase 1B`
- 先把 Blueprint 长尾从“明显缺口”拉到“日常可用”

### 第二轮

- 完成第一优先剩余 `Phase 1C`
- 同时启动第二优先的 `Phase 2A`

### 第三轮

- 完成第二优先剩余部分
- 启动第三优先 `Phase 3A`

### 第四轮

- 推进第三优先剩余部分
- 最后再补第四优先这一层开发辅助面

## 8. 开工前需要固定的口径

- 后续规划仍然只以能力族为准，不追固定工具数量。
- 运行时真实口径继续以 `tools/list` 与 `initialize.capabilities.tools.registeredCount` 为准。
- 验收优先用 `MyProject` 现有宿主，不额外新建 smoke 工程，除非某个能力族确实缺夹具。
- 所有新增工具都继续走 `Docs/Tools-Reference*`、`Docs/CapabilityMatrix*`、`Docs/Tmp/ImplementationRoadmap.zh-CN.md` 的同步更新流程。

## 9. 一句话结论

后续最值的下一步不是再扩散 Step 6，而是按这四个优先级继续做“高价值长尾”：

- 先补 Blueprint 长尾
- 再补 Niagara / Audio / MetaSound
- 再补 Physics + GAS
- 最后补 Search / EngineAPI / Macro / Utility

这样推进，既能继续缩小能力差距，也不会把项目重新拉回“单纯堆工具数量”的路线。
