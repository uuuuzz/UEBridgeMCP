#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "ManageBlueprintInterfacesTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UManageBlueprintInterfacesTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("manage-blueprint-interfaces"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }
};
