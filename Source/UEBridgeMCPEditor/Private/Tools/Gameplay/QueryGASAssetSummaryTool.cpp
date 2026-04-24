// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/QueryGASAssetSummaryTool.h"

#include "Tools/Gameplay/GASToolUtils.h"

#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"

FString UQueryGASAssetSummaryTool::GetToolDescription() const
{
	return TEXT("Query a GAS asset summary for GameplayAbility, GameplayEffect, AttributeSet, or Actor Blueprint bindings.");
}

TMap<FString, FMcpSchemaProperty> UQueryGASAssetSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("GAS asset or Actor Blueprint asset path"), true));
	return Schema;
}

TArray<FString> UQueryGASAssetSummaryTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UQueryGASAssetSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	FString Error;
	UObject* Asset = GASToolUtils::LoadAssetObject(AssetPath, Error);
	if (!Asset)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), Error);
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	UClass* AssetClass = nullptr;
	if (Blueprint)
	{
		AssetClass = Blueprint->GeneratedClass;
	}
	else if (UClass* DirectClass = Cast<UClass>(Asset))
	{
		AssetClass = DirectClass;
	}
	else
	{
		AssetClass = Asset->GetClass();
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("asset_class"), AssetClass ? AssetClass->GetPathName() : Asset->GetClass()->GetPathName());

	if (Blueprint && AssetClass && AssetClass->IsChildOf(UGameplayAbility::StaticClass()))
	{
		Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayAbility(GASToolUtils::GetGameplayAbilityCDO(Blueprint), AssetPath));
		return FMcpToolResult::StructuredJson(Response);
	}
	if (Blueprint && AssetClass && AssetClass->IsChildOf(UGameplayEffect::StaticClass()))
	{
		Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayEffect(GASToolUtils::GetGameplayEffectCDO(Blueprint), AssetPath));
		return FMcpToolResult::StructuredJson(Response);
	}
	if (Blueprint && AssetClass && AssetClass->IsChildOf(UAttributeSet::StaticClass()))
	{
		Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeAttributeSetClass(AssetClass, AssetPath));
		return FMcpToolResult::StructuredJson(Response);
	}
	if (Blueprint && AssetClass && AssetClass->IsChildOf(AActor::StaticClass()))
	{
		Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeActorGASBindings(Blueprint, AssetPath));
		return FMcpToolResult::StructuredJson(Response);
	}
	if (UGameplayEffect* Effect = Cast<UGameplayEffect>(Asset))
	{
		Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayEffect(Effect, AssetPath));
		return FMcpToolResult::StructuredJson(Response);
	}
	if (UGameplayAbility* Ability = Cast<UGameplayAbility>(Asset))
	{
		Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeGameplayAbility(Ability, AssetPath));
		return FMcpToolResult::StructuredJson(Response);
	}

	return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"), TEXT("Asset is not a recognized GAS asset or Actor Blueprint"));
}
