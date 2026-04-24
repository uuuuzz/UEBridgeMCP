// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "GenerateExternalContentTool.generated.h"

UCLASS()
class UEBRIDGEMCPEXTERNALAI_API UGenerateExternalContentTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("generate-external-content"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("generate"); }
	virtual bool MutatesState() const override { return false; }
	virtual bool RequiresGameThread() const override { return false; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("brief") }; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
