// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryAbilitySystemStateTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryAbilitySystemStateTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-ability-system-state"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("query"); }
	virtual FString GetResourceScope() const override { return TEXT("gameplay"); }
};
