# GAS Round 1 Smoke Checklist

Scope: GAS Phase 3B v1. This round covers asset and Actor Blueprint configuration only; it does not validate runtime granting, prediction, ability task graph authoring, or complex GameplayEffect execution calculations.

Host project: `G:\UEProjects\MyProject`

Validation root: `/Game/UEBridgeMCPValidation/GASRound1`

Evidence root: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\GASRound1\<timestamp>`

## Checklist

- Confirm `tools/list` exposes `query-gas-asset-summary`, `create-gameplay-ability`, `create-gameplay-effect`, `create-attribute-set`, `edit-gameplay-effect-modifiers`, and `manage-ability-system-bindings`.
- Use `manage-gameplay-tags` to ensure the smoke tags exist: `Ability.UEBridgeMCP.Round1` and `State.UEBridgeMCP.Active`.
- Create an AttributeSet Blueprint with `create-attribute-set`, including at least `Health` and `Energy` `FGameplayAttributeData` members.
- Query the AttributeSet with `query-gas-asset-summary` and verify the seeded attributes are visible.
- Create a GameplayEffect Blueprint with `create-gameplay-effect`, including duration, period, one granted target tag, and one constant modifier.
- Query the GameplayEffect summary and verify duration, granted tags, and modifier count.
- Run `edit-gameplay-effect-modifiers` with `dry_run=true` for an additional modifier and verify the asset is not changed.
- Run `edit-gameplay-effect-modifiers` with `rollback_on_error=true` to update duration, granted tags, and add a second modifier.
- Create a GameplayAbility Blueprint with `create-gameplay-ability`, including ability tags, activation-owned tags, policy fields, cost GameplayEffect, and cooldown GameplayEffect.
- Query the GameplayAbility summary and verify tags plus cost/cooldown classes.
- Create an Actor Blueprint and use `manage-ability-system-bindings` in dry-run mode for ASC plus ability/effect/attribute-set bindings.
- Apply the same Actor Blueprint binding actions and verify one `AbilitySystemComponent` plus three categorized binding variables.
- Cover a structured negative path for invalid GameplayEffect modifier removal.
- Cover a structured negative path for querying a non-GAS asset.
- Compile the AttributeSet, GameplayEffect, GameplayAbility, and Actor Blueprint with `compile-assets`; all four should compile with no diagnostics.

## Latest Passing Run

- Timestamp: `20260423_203952`
- Evidence: `G:\UEProjects\UEBridgeMCP\Tmp\Validation\GASRound1\20260423_203952`
- Assets:
- `/Game/UEBridgeMCPValidation/GASRound1/AS_GASRound1_20260423_203952`
- `/Game/UEBridgeMCPValidation/GASRound1/GE_GASRound1_20260423_203952`
- `/Game/UEBridgeMCPValidation/GASRound1/GA_GASRound1_20260423_203952`
- `/Game/UEBridgeMCPValidation/GASRound1/BP_GASRound1_20260423_203952`

## Notes

- GameplayEffect granted tags use `UTargetTagsGameplayEffectComponent` on UE 5.7, not direct writes to the internal cached granted tag container.
- GameplayAbility creation must pass `UGameplayAbilityBlueprint::StaticClass()` to `UGameplayAbilitiesBlueprintFactory`.
- Previous exploratory directories under `Tmp\Validation\GASRound1` are retained as debugging evidence but are not the final acceptance run.
