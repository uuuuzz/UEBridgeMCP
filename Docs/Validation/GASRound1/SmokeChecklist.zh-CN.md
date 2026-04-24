# GAS Round 1 Smoke Checklist

范围：GAS Phase 3B v1。本轮只覆盖资产和 Actor Blueprint 配置，不验证 runtime granting、prediction、ability task graph authoring 或复杂 GameplayEffect execution calculation。

宿主项目：`G:\UEProjects\MyProject`

验证资产根目录：`/Game/UEBridgeMCPValidation/GASRound1`

证据根目录：`G:\UEProjects\UEBridgeMCP\Tmp\Validation\GASRound1\<timestamp>`

## Checklist

- 确认 `tools/list` 中存在 `query-gas-asset-summary`、`create-gameplay-ability`、`create-gameplay-effect`、`create-attribute-set`、`edit-gameplay-effect-modifiers`、`manage-ability-system-bindings`。
- 使用 `manage-gameplay-tags` 确保 smoke tags 存在：`Ability.UEBridgeMCP.Round1` 与 `State.UEBridgeMCP.Active`。
- 用 `create-attribute-set` 创建 AttributeSet Blueprint，并至少写入 `Health` 与 `Energy` 两个 `FGameplayAttributeData` 成员。
- 用 `query-gas-asset-summary` 查询 AttributeSet，确认属性可见。
- 用 `create-gameplay-effect` 创建 GameplayEffect Blueprint，包含 duration、period、一个 granted target tag 和一个常量 modifier。
- 查询 GameplayEffect 摘要，确认 duration、granted tags 和 modifier count。
- 对 `edit-gameplay-effect-modifiers` 跑 `dry_run=true`，验证不会修改资产。
- 对 `edit-gameplay-effect-modifiers` 跑 `rollback_on_error=true` 正向修改 duration、granted tags，并新增第二个 modifier。
- 用 `create-gameplay-ability` 创建 GameplayAbility Blueprint，包含 ability tags、activation-owned tags、policy 字段、cost GameplayEffect 和 cooldown GameplayEffect。
- 查询 GameplayAbility 摘要，确认 tags 与 cost/cooldown classes。
- 创建 Actor Blueprint，并对 `manage-ability-system-bindings` 跑 dry-run，覆盖 ASC 与 ability/effect/attribute-set bindings。
- 应用同一组 Actor Blueprint binding actions，确认有 1 个 `AbilitySystemComponent` 和 3 个分类 binding 变量。
- 覆盖一条 invalid GameplayEffect modifier removal 的结构化负路径。
- 覆盖一条 non-GAS asset query 的结构化负路径。
- 用 `compile-assets` 编译 AttributeSet、GameplayEffect、GameplayAbility 和 Actor Blueprint，四个资产均应无 diagnostics。

## 最新通过记录

- Timestamp: `20260423_203952`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\GASRound1\20260423_203952`
- Assets:
- `/Game/UEBridgeMCPValidation/GASRound1/AS_GASRound1_20260423_203952`
- `/Game/UEBridgeMCPValidation/GASRound1/GE_GASRound1_20260423_203952`
- `/Game/UEBridgeMCPValidation/GASRound1/GA_GASRound1_20260423_203952`
- `/Game/UEBridgeMCPValidation/GASRound1/BP_GASRound1_20260423_203952`

## Notes

- UE 5.7 中 GameplayEffect granted tags 需要通过 `UTargetTagsGameplayEffectComponent` 写入，不能直接写内部 cached granted tag container。
- GameplayAbility 创建时，`UGameplayAbilitiesBlueprintFactory` 必须搭配 `UGameplayAbilityBlueprint::StaticClass()`。
- `Tmp\Validation\GASRound1` 下早期探索目录会保留为调试证据，但不作为最终验收记录。
