// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditorInteractionTools.generated.h"

UCLASS()
class UEBRIDGEMCPEDITOR_API UEditEditorSelectionTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-editor-selection"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("mutation"); }
	virtual FString GetResourceScope() const override { return TEXT("editor_selection"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};

UCLASS()
class UEBRIDGEMCPEDITOR_API UEditViewportCameraTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-viewport-camera"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("mutation"); }
	virtual FString GetResourceScope() const override { return TEXT("editor_viewport"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};

UCLASS()
class UEBRIDGEMCPEDITOR_API URunEditorCommandTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("run-editor-command"); }
	virtual FString GetToolDescription() const override;
	virtual FString GetToolKind() const override { return TEXT("utility"); }
	virtual FString GetResourceScope() const override { return TEXT("editor"); }
	virtual bool MutatesState() const override { return true; }
	virtual bool SupportsDryRun() const override { return true; }
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual FMcpToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context) override;
};
