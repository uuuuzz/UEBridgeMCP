# Landscape Round 1 Provenance

本轮是在 Foliage Round 1 之后补齐的另一个 World Production 高价值缺口：之前 Landscape 已有写工具，但缺少自包含的 create/query 验证闭环。

实现变更：

- 新增条件工具 `query-landscape-summary`，返回 Landscape actor、transform、bounds、Landscape GUID、component sizing、layer settings、可选 component 摘要和可选 landscape-coordinate height samples。
- 新增条件工具 `create-landscape`，用于在 editor world 创建小型平坦 Landscape，支持 dry-run、component count、sections per component、quads per section、初始 local height、transform 和 save 控制。
- 共享 Landscape 序列化逻辑放在 `LandscapeToolUtils`，其中高度采样通过 `FLandscapeEditDataInterface` 完成。
- `edit-landscape-region` 通过新查询工具的 height sample 完成正向验证，因此 smoke 证明的是数据确实变化，而不只是工具返回成功。
- 避开了一个 UE 5.7 链接兼容问题：不再调用未导出的 `FWeightmapLayerAllocationInfo::GetLayerName()` helper，而是通过 `ULandscapeLayerInfoObject` 读取 weightmap layer 名称。

验证运行：

- 时间戳：`20260423_235345`
- 证据目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\LandscapeRound1\20260423_235345`
- 创建的 Actor label：`LandscapeRound1_20260423_235345`
- 创建分辨率：`8x8`
- 高度证据：采样点 `[1,1]` 从 `0` 变为 `128`；采样点 `[4,4]` 保持 `0`。

构建：

- 命令：`Build.bat MyProjectEditor Win64 Development -Project=G:\UEProjects\MyProject\MyProject.uproject -WaitMutex -NoHotReload -MaxParallelActions=4`
- 结果：成功。

范围边界：

- 本轮覆盖小型平坦 Landscape 创建、结构摘要、component/layer 报告、高度采样、矩形高度编辑、dry-run 行为、缺失 actor 结构化错误和清理。
- 本轮不新增 landscape material graph authoring、layer info asset 创建、sculpt brush simulation、spline-to-landscape deformation、streaming proxy management 或 World Partition landscape region generation。
