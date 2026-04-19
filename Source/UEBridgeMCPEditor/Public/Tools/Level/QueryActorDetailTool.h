// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryActorDetailTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryActorDetailTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-actor-detail"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("detail"); }
	virtual FString GetResourceScope() const override { return TEXT("actor"); }
};
