// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "QueryMaterialInstanceTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UQueryMaterialInstanceTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-material-instance"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("detail"); }
	virtual FString GetResourceScope() const override { return TEXT("material_instance"); }
};
