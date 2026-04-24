// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditPhysicsConstraintTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UEditPhysicsConstraintTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-physics-constraint"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {}; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("edit"); }
	virtual FString GetResourceScope() const override { return TEXT("physics"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
};
