// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CreateLandscapeTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UCreateLandscapeTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("create-landscape"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("create"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
