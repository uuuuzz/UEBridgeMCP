#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "AnalyzeBlueprintCompileResultsTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UAnalyzeBlueprintCompileResultsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("analyze-blueprint-compile-results"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }
};
