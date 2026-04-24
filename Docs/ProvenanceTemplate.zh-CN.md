# UEBridgeMCP Provenance 模板

当一项工作参考了其他插件、产品或 benchmark 的能力面时，请在 PR 描述、发布说明或内部实现记录里附上这段说明。

```md
## Provenance

- 能力族：
- 路线步骤：
- 对应需求 / issue / 里程碑：

### 参考输入
- 产品判断：
- 黑盒体验 / 截图：
- 公开文档 / README / 功能介绍页：
- Unreal 或 MCP 标准参考：

### 明确未使用的内容
- 未复制任何源代码。
- 未复制私有辅助函数结构、文件拆分方式或注册顺序。
- 未复制 prompt 文本、schema 描述、错误措辞、截图或营销文案。

### 独立设计说明
- 这次为 UEBridgeMCP 选用的工具名：
- 为什么这些命名符合 UEBridgeMCP 现有风格：
- 为什么 API 采用 `query-*` / `edit-*` / `create-*` / `manage-*` / `generate-*`：
- 批处理 / 事务 / `dry_run` / `save` / `rollback_on_error` 的设计理由：
- 与参考产品刻意保持不同的地方：

### 验证
- 构建 / 编译验证：
- 人工 smoke test：
- 当前缺口 / 下一步：
```

补充说明：

- 如果某些词本来就是 Unreal 或 MCP 里的标准术语，可以正常使用。
- 只要是“像文案”的内容，都应该改写成 UEBridgeMCP 自己的表达。
- 拿不准时，优先描述用户目标和你的独立设计，而不是复述别的产品是怎么实现的。
