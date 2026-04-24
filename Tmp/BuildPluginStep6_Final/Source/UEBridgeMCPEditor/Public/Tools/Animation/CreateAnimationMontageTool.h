// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CreateAnimationMontageTool.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UCreateAnimationMontageTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("create-animation-montage"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return { TEXT("asset_path"), TEXT("sequence_paths") }; }
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
	virtual FString GetToolKind() const override { return TEXT("create"); }
	virtual FString GetResourceScope() const override { return TEXT("animation"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsSave() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
};
