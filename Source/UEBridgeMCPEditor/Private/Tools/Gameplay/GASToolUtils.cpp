// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/GASToolUtils.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AttributeSet.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Factories/BlueprintFactory.h"
#include "GameplayAbilitiesBlueprintFactory.h"
#include "GameplayAbilityBlueprint.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTagsManager.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScalableFloat.h"

namespace
{
	FString MakeObjectPathFromAssetPath(const FString& AssetPath)
	{
		if (AssetPath.Contains(TEXT(".")))
		{
			return AssetPath;
		}
		return FString::Printf(TEXT("%s.%s"), *AssetPath, *FPackageName::GetShortName(AssetPath));
	}

	FString MakeGeneratedClassPath(const FString& AssetPath)
	{
		if (AssetPath.EndsWith(TEXT("_C")) || AssetPath.Contains(TEXT("_C'")))
		{
			return AssetPath;
		}
		const FString ObjectPath = MakeObjectPathFromAssetPath(AssetPath);
		return ObjectPath + TEXT("_C");
	}

	TSharedPtr<FJsonObject> MakeClassObject(UClass* Class)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (Class)
		{
			Object->SetStringField(TEXT("name"), Class->GetName());
			Object->SetStringField(TEXT("path"), Class->GetPathName());
		}
		else
		{
			Object->SetBoolField(TEXT("is_null"), true);
		}
		return Object;
	}

	TSharedPtr<FJsonObject> MakeModifierObject(const FGameplayModifierInfo& Modifier, int32 Index)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("attribute"), GASToolUtils::AttributeToString(Modifier.Attribute));
		Object->SetStringField(TEXT("operation"), GASToolUtils::ModifierOpToString(Modifier.ModifierOp));

		float Magnitude = 0.0f;
		if (Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.0f, Magnitude))
		{
			Object->SetNumberField(TEXT("magnitude"), Magnitude);
			Object->SetStringField(TEXT("magnitude_type"), TEXT("scalable_float"));
		}
		else
		{
			Object->SetStringField(TEXT("magnitude_type"), TEXT("dynamic"));
		}
		return Object;
	}

	void SetScalableFloat(FScalableFloat& Target, float Value)
	{
		Target = FScalableFloat(Value);
	}

	void SetModifierMagnitude(FGameplayEffectModifierMagnitude& Target, float Value)
	{
		Target = FGameplayEffectModifierMagnitude(FScalableFloat(Value));
	}

	FString NormalizeEnumToken(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT("_"));
		return Value;
	}

	FString MakeBindingVariableName(const FString& Prefix, UClass* BoundClass)
	{
		const FString RawName = BoundClass ? BoundClass->GetName().Replace(TEXT("_C"), TEXT("")) : TEXT("Class");
		FString CleanName = RawName;
		CleanName.ReplaceInline(TEXT("."), TEXT("_"));
		CleanName.ReplaceInline(TEXT("/"), TEXT("_"));
		return Prefix + CleanName;
	}
}

namespace GASToolUtils
{
	UBlueprint* CreateGASBlueprintAsset(const FString& AssetPath, UClass* ParentClass, FString& OutError)
	{
		return GameplayToolUtils::CreateBlueprintAsset(AssetPath, ParentClass, OutError);
	}

	UBlueprint* CreateGameplayAbilityBlueprintAsset(const FString& AssetPath, UClass* ParentClass, FString& OutError)
	{
		if (!ParentClass || !ParentClass->IsChildOf(UGameplayAbility::StaticClass()))
		{
			OutError = TEXT("Parent class must derive from UGameplayAbility");
			return nullptr;
		}
		if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, OutError))
		{
			return nullptr;
		}
		if (FMcpAssetModifier::AssetExists(AssetPath))
		{
			OutError = FString::Printf(TEXT("Asset already exists: %s"), *AssetPath);
			return nullptr;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		const FString AssetName = FPackageName::GetShortName(AssetPath);
		UGameplayAbilitiesBlueprintFactory* Factory = NewObject<UGameplayAbilitiesBlueprintFactory>();
		Factory->ParentClass = ParentClass;
		Factory->BlueprintType = BPTYPE_Normal;

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UObject* CreatedObject = AssetTools.CreateAsset(AssetName, PackagePath, UGameplayAbilityBlueprint::StaticClass(), Factory);
		UBlueprint* CreatedBlueprint = Cast<UBlueprint>(CreatedObject);
		if (!CreatedBlueprint)
		{
			OutError = FString::Printf(TEXT("Failed to create GameplayAbility Blueprint: %s"), *AssetPath);
			return nullptr;
		}

		FMcpAssetModifier::MarkPackageDirty(CreatedBlueprint);
		return CreatedBlueprint;
	}

	bool CompileAndSaveBlueprint(UBlueprint* Blueprint, bool bCompile, bool bSave, TSharedPtr<FJsonObject>& OutCompile, FString& OutError)
	{
		OutCompile = MakeShareable(new FJsonObject);
		OutCompile->SetBoolField(TEXT("attempted"), bCompile);
		OutCompile->SetBoolField(TEXT("success"), true);

		if (bCompile)
		{
			FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);
			if (!FMcpAssetModifier::CompileBlueprint(Blueprint, OutError))
			{
				OutCompile->SetBoolField(TEXT("success"), false);
				OutCompile->SetStringField(TEXT("error"), OutError);
				return false;
			}
		}

		if (bSave)
		{
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, OutError))
			{
				return false;
			}
		}
		return true;
	}

	UBlueprint* LoadBlueprint(const FString& AssetPath, FString& OutError)
	{
		UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, OutError);
		if (!Blueprint && OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Blueprint asset not found: %s"), *AssetPath);
		}
		return Blueprint;
	}

	UObject* LoadAssetObject(const FString& AssetPath, FString& OutError)
	{
		UObject* Object = FMcpAssetModifier::LoadAssetByPath<UObject>(AssetPath, OutError);
		if (!Object)
		{
			Object = LoadObject<UObject>(nullptr, *MakeObjectPathFromAssetPath(AssetPath));
		}
		if (!Object && OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		}
		return Object;
	}

	UClass* ResolveClass(const FString& ClassOrAssetPath, FString& OutError)
	{
		if (ClassOrAssetPath.IsEmpty())
		{
			OutError = TEXT("Class path is required");
			return nullptr;
		}

		if (UClass* DirectClass = LoadObject<UClass>(nullptr, *ClassOrAssetPath))
		{
			return DirectClass;
		}
		if (UClass* GeneratedClass = LoadObject<UClass>(nullptr, *MakeGeneratedClassPath(ClassOrAssetPath)))
		{
			return GeneratedClass;
		}
		if (UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(ClassOrAssetPath, OutError))
		{
			if (Blueprint->GeneratedClass)
			{
				return Blueprint->GeneratedClass;
			}
			OutError = FString::Printf(TEXT("Blueprint has no generated class: %s"), *ClassOrAssetPath);
			return nullptr;
		}

		OutError = FString::Printf(TEXT("Class or Blueprint asset not found: %s"), *ClassOrAssetPath);
		return nullptr;
	}

	UClass* ResolveSubclass(const FString& ClassOrAssetPath, UClass* RequiredBaseClass, FString& OutError)
	{
		UClass* Class = ResolveClass(ClassOrAssetPath, OutError);
		if (!Class)
		{
			return nullptr;
		}
		if (RequiredBaseClass && !Class->IsChildOf(RequiredBaseClass))
		{
			OutError = FString::Printf(TEXT("Class '%s' does not derive from '%s'"), *Class->GetPathName(), *RequiredBaseClass->GetName());
			return nullptr;
		}
		return Class;
	}

	UGameplayEffect* GetGameplayEffectCDO(UBlueprint* Blueprint)
	{
		return Blueprint && Blueprint->GeneratedClass ? Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	}

	UGameplayAbility* GetGameplayAbilityCDO(UBlueprint* Blueprint)
	{
		return Blueprint && Blueprint->GeneratedClass ? Cast<UGameplayAbility>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	}

	bool ParseDurationPolicy(const FString& Value, EGameplayEffectDurationType& OutPolicy)
	{
		const FString Token = NormalizeEnumToken(Value).ToLower();
		if (Token == TEXT("instant"))
		{
			OutPolicy = EGameplayEffectDurationType::Instant;
			return true;
		}
		if (Token == TEXT("duration") || Token == TEXT("has_duration") || Token == TEXT("hasduration"))
		{
			OutPolicy = EGameplayEffectDurationType::HasDuration;
			return true;
		}
		if (Token == TEXT("infinite"))
		{
			OutPolicy = EGameplayEffectDurationType::Infinite;
			return true;
		}
		return false;
	}

	FString DurationPolicyToString(EGameplayEffectDurationType Policy)
	{
		switch (Policy)
		{
		case EGameplayEffectDurationType::Instant: return TEXT("instant");
		case EGameplayEffectDurationType::HasDuration: return TEXT("has_duration");
		case EGameplayEffectDurationType::Infinite: return TEXT("infinite");
		default: return TEXT("unknown");
		}
	}

	bool ParseModifierOp(const FString& Value, TEnumAsByte<EGameplayModOp::Type>& OutOp)
	{
		const FString Token = NormalizeEnumToken(Value).ToLower();
		if (Token == TEXT("add") || Token == TEXT("additive"))
		{
			OutOp = EGameplayModOp::Additive;
			return true;
		}
		if (Token == TEXT("multiply") || Token == TEXT("multiplicative"))
		{
			OutOp = EGameplayModOp::Multiplicitive;
			return true;
		}
		if (Token == TEXT("divide") || Token == TEXT("division"))
		{
			OutOp = EGameplayModOp::Division;
			return true;
		}
		if (Token == TEXT("override"))
		{
			OutOp = EGameplayModOp::Override;
			return true;
		}
		return false;
	}

	FString ModifierOpToString(TEnumAsByte<EGameplayModOp::Type> Op)
	{
		switch (Op.GetValue())
		{
		case EGameplayModOp::Additive: return TEXT("additive");
		case EGameplayModOp::Multiplicitive: return TEXT("multiplicative");
		case EGameplayModOp::Division: return TEXT("division");
		case EGameplayModOp::Override: return TEXT("override");
		default: return TEXT("unknown");
		}
	}

	bool ParseAbilityInstancingPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityInstancingPolicy::Type>& OutPolicy)
	{
		const FString Token = NormalizeEnumToken(Value).ToLower();
		if (Token == TEXT("non_instanced") || Token == TEXT("noninstanced"))
		{
			OutPolicy = EGameplayAbilityInstancingPolicy::NonInstanced;
			return true;
		}
		if (Token == TEXT("instanced_per_actor") || Token == TEXT("instancedperactor"))
		{
			OutPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
			return true;
		}
		if (Token == TEXT("instanced_per_execution") || Token == TEXT("instancedperexecution"))
		{
			OutPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
			return true;
		}
		return false;
	}

	bool ParseAbilityNetExecutionPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityNetExecutionPolicy::Type>& OutPolicy)
	{
		const FString Token = NormalizeEnumToken(Value).ToLower();
		if (Token == TEXT("local_predicted") || Token == TEXT("localpredicted"))
		{
			OutPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
			return true;
		}
		if (Token == TEXT("local_only") || Token == TEXT("localonly"))
		{
			OutPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
			return true;
		}
		if (Token == TEXT("server_initiated") || Token == TEXT("serverinitiated"))
		{
			OutPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
			return true;
		}
		if (Token == TEXT("server_only") || Token == TEXT("serveronly"))
		{
			OutPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
			return true;
		}
		return false;
	}

	bool ParseAbilityReplicationPolicy(const FString& Value, TEnumAsByte<EGameplayAbilityReplicationPolicy::Type>& OutPolicy)
	{
		const FString Token = NormalizeEnumToken(Value).ToLower();
		if (Token == TEXT("replicate_yes") || Token == TEXT("replicateyes") || Token == TEXT("yes"))
		{
			OutPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
			return true;
		}
		if (Token == TEXT("replicate_no") || Token == TEXT("replicateno") || Token == TEXT("no"))
		{
			OutPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
			return true;
		}
		return false;
	}

	bool BuildTagContainer(const TArray<TSharedPtr<FJsonValue>>* TagsArray, FGameplayTagContainer& OutTags, FString& OutError)
	{
		OutTags.Reset();
		if (!TagsArray)
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
		{
			FString TagString;
			if (!TagValue.IsValid() || !TagValue->TryGetString(TagString) || TagString.IsEmpty())
			{
				continue;
			}
			FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagString), false);
			if (!Tag.IsValid())
			{
				OutError = FString::Printf(TEXT("Gameplay tag does not exist: %s"), *TagString);
				return false;
			}
			OutTags.AddTag(Tag);
		}
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> SerializeTagContainer(const FGameplayTagContainer& Tags)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FGameplayTag& Tag : Tags)
		{
			Values.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
		}
		return Values;
	}

	bool SetTagContainerProperty(UObject* Target, const FString& PropertyName, const FGameplayTagContainer& Tags, FString& OutError)
	{
		if (!Target)
		{
			OutError = TEXT("Target object is null");
			return false;
		}
		FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
		FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty || StructProperty->Struct != FGameplayTagContainer::StaticStruct())
		{
			OutError = FString::Printf(TEXT("GameplayTagContainer property not found: %s"), *PropertyName);
			return false;
		}
		void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Target);
		*static_cast<FGameplayTagContainer*>(ValuePtr) = Tags;
		return true;
	}

	bool SetGameplayEffectGrantedTags(UGameplayEffect* Effect, const FGameplayTagContainer& Tags, FString& OutError)
	{
		if (!Effect)
		{
			OutError = TEXT("GameplayEffect is null");
			return false;
		}

		FInheritedTagContainer TagChanges;
		for (const FGameplayTag& Tag : Tags)
		{
			TagChanges.AddTag(Tag);
		}

		UTargetTagsGameplayEffectComponent& TargetTagsComponent = Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		TargetTagsComponent.Modify();
		TargetTagsComponent.SetAndApplyTargetTagChanges(TagChanges);
		Effect->OnGameplayEffectChanged();
		return true;
	}

	bool ResolveGameplayAttribute(const TSharedPtr<FJsonObject>& Source, FGameplayAttribute& OutAttribute, FString& OutError)
	{
		FString AttributeSetClassPath;
		FString AttributeName;
		Source->TryGetStringField(TEXT("attribute_set_class"), AttributeSetClassPath);
		Source->TryGetStringField(TEXT("attribute_name"), AttributeName);

		if (AttributeName.IsEmpty())
		{
			FString Combined;
			Source->TryGetStringField(TEXT("attribute"), Combined);
			if (Combined.Contains(TEXT(".")))
			{
				FString Left;
				Combined.Split(TEXT("."), &Left, &AttributeName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (AttributeSetClassPath.IsEmpty())
				{
					AttributeSetClassPath = Left;
				}
			}
			else
			{
				AttributeName = Combined;
			}
		}

		if (AttributeSetClassPath.IsEmpty() || AttributeName.IsEmpty())
		{
			OutError = TEXT("'attribute_set_class' and 'attribute_name' are required for modifier attributes");
			return false;
		}

		UClass* AttributeSetClass = ResolveSubclass(AttributeSetClassPath, UAttributeSet::StaticClass(), OutError);
		if (!AttributeSetClass)
		{
			return false;
		}

		FProperty* Property = FindFProperty<FProperty>(AttributeSetClass, FName(*AttributeName));
		if (!Property)
		{
			OutError = FString::Printf(TEXT("Attribute property '%s' not found on class '%s'"), *AttributeName, *AttributeSetClass->GetName());
			return false;
		}

		OutAttribute = FGameplayAttribute(Property);
		if (!OutAttribute.IsValid())
		{
			OutError = FString::Printf(TEXT("Property '%s' is not a valid gameplay attribute"), *AttributeName);
			return false;
		}
		return true;
	}

	FString AttributeToString(const FGameplayAttribute& Attribute)
	{
		if (!Attribute.IsValid())
		{
			return TEXT("");
		}
		return FString::Printf(TEXT("%s.%s"), *Attribute.GetAttributeSetClass()->GetName(), *Attribute.GetName());
	}

	TSharedPtr<FJsonObject> SerializeGameplayEffect(UGameplayEffect* Effect, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("gas_type"), TEXT("gameplay_effect"));
		if (!Effect)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("class"), Effect->GetClass()->GetPathName());
		Object->SetStringField(TEXT("duration_policy"), DurationPolicyToString(Effect->DurationPolicy));
		float Duration = 0.0f;
		if (Effect->DurationMagnitude.GetStaticMagnitudeIfPossible(1.0f, Duration))
		{
			Object->SetNumberField(TEXT("duration_seconds"), Duration);
		}
		Object->SetNumberField(TEXT("period"), Effect->Period.GetValueAtLevel(1.0f));
		Object->SetArrayField(TEXT("granted_tags"), SerializeTagContainer(Effect->GetGrantedTags()));
		Object->SetNumberField(TEXT("modifier_count"), Effect->Modifiers.Num());

		TArray<TSharedPtr<FJsonValue>> Modifiers;
		for (int32 Index = 0; Index < Effect->Modifiers.Num(); ++Index)
		{
			Modifiers.Add(MakeShareable(new FJsonValueObject(MakeModifierObject(Effect->Modifiers[Index], Index))));
		}
		Object->SetArrayField(TEXT("modifiers"), Modifiers);
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeGameplayAbility(UGameplayAbility* Ability, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("gas_type"), TEXT("gameplay_ability"));
		if (!Ability)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("class"), Ability->GetClass()->GetPathName());
		Object->SetStringField(TEXT("instancing_policy"), StaticEnum<EGameplayAbilityInstancingPolicy::Type>()->GetNameStringByValue(Ability->GetInstancingPolicy()));
		Object->SetStringField(TEXT("net_execution_policy"), StaticEnum<EGameplayAbilityNetExecutionPolicy::Type>()->GetNameStringByValue(Ability->GetNetExecutionPolicy()));
		Object->SetStringField(TEXT("replication_policy"), StaticEnum<EGameplayAbilityReplicationPolicy::Type>()->GetNameStringByValue(Ability->GetReplicationPolicy()));
		Object->SetArrayField(TEXT("ability_tags"), SerializeTagContainer(Ability->GetAssetTags()));
		Object->SetObjectField(TEXT("cost_gameplay_effect_class"), MakeClassObject(Ability->GetCostGameplayEffect() ? Ability->GetCostGameplayEffect()->GetClass() : nullptr));
		Object->SetObjectField(TEXT("cooldown_gameplay_effect_class"), MakeClassObject(Ability->GetCooldownGameplayEffect() ? Ability->GetCooldownGameplayEffect()->GetClass() : nullptr));
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeAttributeSetClass(UClass* AttributeSetClass, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("gas_type"), TEXT("attribute_set"));
		Object->SetBoolField(TEXT("valid"), AttributeSetClass != nullptr);
		if (!AttributeSetClass)
		{
			return Object;
		}

		Object->SetStringField(TEXT("class"), AttributeSetClass->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Attributes;
		for (TFieldIterator<FProperty> It(AttributeSetClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || Property->GetOwnerClass() == UAttributeSet::StaticClass())
			{
				continue;
			}
			const bool bLooksLikeAttribute = Property->IsA<FStructProperty>() || Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>();
			if (!bLooksLikeAttribute)
			{
				continue;
			}
			TSharedPtr<FJsonObject> AttributeObject = MakeShareable(new FJsonObject);
			AttributeObject->SetStringField(TEXT("name"), Property->GetName());
			AttributeObject->SetStringField(TEXT("type"), Property->GetCPPType());
			AttributeObject->SetStringField(TEXT("owner_class"), Property->GetOwnerClass() ? Property->GetOwnerClass()->GetName() : TEXT(""));
			Attributes.Add(MakeShareable(new FJsonValueObject(AttributeObject)));
		}
		Object->SetNumberField(TEXT("attribute_count"), Attributes.Num());
		Object->SetArrayField(TEXT("attributes"), Attributes);
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeActorGASBindings(UBlueprint* Blueprint, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("asset_path"), AssetPath);
		Object->SetStringField(TEXT("gas_type"), TEXT("actor_bindings"));
		Object->SetBoolField(TEXT("valid"), Blueprint != nullptr);
		if (!Blueprint)
		{
			return Object;
		}

		TArray<TSharedPtr<FJsonValue>> Components;
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<UAbilitySystemComponent>())
				{
					TSharedPtr<FJsonObject> ComponentObject = MakeShareable(new FJsonObject);
					ComponentObject->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
					ComponentObject->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetPathName());
					Components.Add(MakeShareable(new FJsonValueObject(ComponentObject)));
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> BindingVariables;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			FString Kind;
			FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, TEXT("UEBridgeMCP.GASBindingKind"), Kind);
			if (!Kind.IsEmpty())
			{
				FString BoundClassPath;
				FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, Variable.VarName, nullptr, TEXT("UEBridgeMCP.GASBindingClass"), BoundClassPath);
				TSharedPtr<FJsonObject> VariableObject = MakeShareable(new FJsonObject);
				VariableObject->SetStringField(TEXT("name"), Variable.VarName.ToString());
				VariableObject->SetStringField(TEXT("binding_kind"), Kind);
				VariableObject->SetStringField(TEXT("class_path"), BoundClassPath);
				BindingVariables.Add(MakeShareable(new FJsonValueObject(VariableObject)));
			}
		}

		Object->SetNumberField(TEXT("ability_system_component_count"), Components.Num());
		Object->SetArrayField(TEXT("ability_system_components"), Components);
		Object->SetNumberField(TEXT("binding_variable_count"), BindingVariables.Num());
		Object->SetArrayField(TEXT("binding_variables"), BindingVariables);
		return Object;
	}

	bool AddAttributeVariable(UBlueprint* Blueprint, const FString& Name, const FString& Category, FString& OutError)
	{
		if (!Blueprint || Name.IsEmpty())
		{
			OutError = TEXT("Blueprint and attribute name are required");
			return false;
		}
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*Name)) != INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Attribute variable already exists: %s"), *Name);
			return false;
		}

		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FGameplayAttributeData::StaticStruct();
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), PinType))
		{
			OutError = FString::Printf(TEXT("Failed to add attribute variable: %s"), *Name);
			return false;
		}
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*Name), nullptr, FText::FromString(Category.IsEmpty() ? TEXT("Attributes") : Category), true);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return true;
	}

	bool AddAbilitySystemComponent(UBlueprint* Blueprint, const FString& ComponentName, const FString& ReplicationMode, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			OutError = TEXT("Blueprint does not have a SimpleConstructionScript");
			return false;
		}
		if (FindSCSNode(Blueprint, ComponentName))
		{
			OutResult->SetStringField(TEXT("component_name"), ComponentName);
			OutResult->SetBoolField(TEXT("already_exists"), true);
			return true;
		}

		USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(UAbilitySystemComponent::StaticClass(), FName(*ComponentName));
		if (!NewNode)
		{
			OutError = TEXT("Failed to create AbilitySystemComponent SCS node");
			return false;
		}
		if (UAbilitySystemComponent* Template = Cast<UAbilitySystemComponent>(NewNode->ComponentTemplate))
		{
			const FString Token = NormalizeEnumToken(ReplicationMode).ToLower();
			if (Token == TEXT("minimal"))
			{
				Template->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
			}
			else if (Token == TEXT("mixed") || Token.IsEmpty())
			{
				Template->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
			}
			else if (Token == TEXT("full"))
			{
				Template->SetReplicationMode(EGameplayEffectReplicationMode::Full);
			}
			else
			{
				OutError = FString::Printf(TEXT("Unsupported AbilitySystemComponent replication mode: %s"), *ReplicationMode);
				return false;
			}
		}

		Blueprint->SimpleConstructionScript->AddNode(NewNode);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		OutResult->SetStringField(TEXT("component_name"), NewNode->GetVariableName().ToString());
		OutResult->SetStringField(TEXT("component_class"), UAbilitySystemComponent::StaticClass()->GetPathName());
		OutResult->SetStringField(TEXT("replication_mode"), ReplicationMode.IsEmpty() ? TEXT("mixed") : ReplicationMode);
		return true;
	}

	bool AddClassBindingVariable(UBlueprint* Blueprint, const FString& VariableName, UClass* BaseClass, UClass* BoundClass, const FString& BindingKind, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
	{
		if (!Blueprint || !BaseClass || !BoundClass)
		{
			OutError = TEXT("Blueprint, base class, and bound class are required");
			return false;
		}
		if (!BoundClass->IsChildOf(BaseClass))
		{
			OutError = FString::Printf(TEXT("Bound class '%s' does not derive from '%s'"), *BoundClass->GetName(), *BaseClass->GetName());
			return false;
		}

		const FString FinalVariableName = VariableName.IsEmpty()
			? MakeBindingVariableName(BindingKind == TEXT("ability") ? TEXT("GA_") : BindingKind == TEXT("gameplay_effect") ? TEXT("GE_") : TEXT("AS_"), BoundClass)
			: VariableName;
		const FName VarName(*FinalVariableName);
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName) == INDEX_NONE)
		{
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			PinType.PinSubCategoryObject = BaseClass;
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType, BoundClass->GetPathName()))
			{
				OutError = FString::Printf(TEXT("Failed to add GAS binding variable: %s"), *FinalVariableName);
				return false;
			}
		}
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarName, nullptr, TEXT("UEBridgeMCP.GASBindingKind"), BindingKind);
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarName, nullptr, TEXT("UEBridgeMCP.GASBindingClass"), BoundClass->GetPathName());
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, VarName, nullptr, FText::FromString(TEXT("GAS")), true);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

		OutResult->SetStringField(TEXT("variable_name"), FinalVariableName);
		OutResult->SetStringField(TEXT("binding_kind"), BindingKind);
		OutResult->SetStringField(TEXT("bound_class"), BoundClass->GetPathName());
		return true;
	}

	USCS_Node* FindSCSNode(UBlueprint* Blueprint, const FString& ComponentName)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
		{
			return nullptr;
		}
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}
}
