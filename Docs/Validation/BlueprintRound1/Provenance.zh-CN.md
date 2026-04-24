# Blueprint 第一轮 Provenance

## 范围

这份 provenance 说明只覆盖 Blueprint 第一轮交付：

- Phase 1A + 1B
- `edit-blueprint-members` 的成员层扩展
- `edit-blueprint-graph` 的图层扩展
- 6 个 Blueprint 薄包装/查询工具
- `MyProject` 宿主验证所需的文档与 smoke checklist 收口

它不声明交付 Phase 1C、编译结果深分析、fixup pattern、Niagara、Physics 或 Search。

## 主要代码来源

- `Source/UEBridgeMCPEditor/Public/Tools/Write/CreateAssetTool.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Write/CreateAssetTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintToolUtils.h`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/BlueprintToolUtils.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/EditBlueprintMembersTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/EditBlueprintGraphTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/CreateBlueprintFunctionTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/CreateBlueprintEventTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/EditBlueprintFunctionSignatureTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/ManageBlueprintInterfacesTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/LayoutBlueprintGraphTool.cpp`
- `Source/UEBridgeMCPEditor/Private/Tools/Blueprint/QueryBlueprintFindingsTool.cpp`
- `Source/UEBridgeMCPEditor/Private/UEBridgeMCPEditor.cpp`

## 文档来源

- `Docs/Tools-Reference.md`
- `Docs/Tools-Reference.zh-CN.md`
- `Docs/CapabilityMatrix.md`
- `Docs/CapabilityMatrix.zh-CN.md`
- `Docs/Validation/BlueprintRound1/SmokeChecklist.md`
- `Docs/Validation/BlueprintRound1/SmokeChecklist.zh-CN.md`

## 预期运行时证据

当宿主工程 smoke 被实际记录时，建议统一落到：

- `G:\UEProjects\UEBridgeMCP\Tmp\Validation\BlueprintRound1\<timestamp>`

建议留存的证据包括：

- `initialize` 响应片段
- 含有新 Blueprint 工具的 `tools/list` 片段
- 各个 wrapper 工具与 `query-blueprint-findings` 的请求/响应样例
- `/Game/UEBridgeMCPValidation/BlueprintRound1` 下最终 Blueprint 与 Blueprint Interface 的资产路径
- 运行时创建的 interface function 声明请求/响应样例；为了让 `sync_graphs` 产生真实接口图，优先使用非 void 签名
- 最终 Blueprint 状态的 compile/save 结果

## 当前验证定位

Blueprint 第一轮明确复用现有宿主工程：

- 宿主工程：`G:\UEProjects\MyProject`
- 验证资产根目录：`/Game/UEBridgeMCPValidation/BlueprintRound1`
- interface 验证通过运行时创建的 Blueprint Interface 资产完成，不依赖 repo-tracked fixture
- 本轮不额外引入新的自动化 harness

在运行时 smoke 之前，构建成功本身也是 provenance 的一部分：

- `MyProject` 下编辑器构建成功
- 编辑器模块中已存在新工具注册
- 文档已同步说明运行时工具可见性与验证面

## 已知边界

- `query-blueprint-findings` 在 v1 中故意保持为非编译式、结构扫描型查询。
- 后续 Blueprint 第二轮能力需要单独产出 provenance，不应默默扩展这份说明。
