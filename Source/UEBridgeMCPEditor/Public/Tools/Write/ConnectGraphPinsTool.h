// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "ConnectGraphPinsTool.generated.h"

/**
 * Connect two pins in a Blueprint or Material graph.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UConnectGraphPinsTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("connect-graph-pins"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("write"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool RequiresGameThread() const override { return true; }
};
