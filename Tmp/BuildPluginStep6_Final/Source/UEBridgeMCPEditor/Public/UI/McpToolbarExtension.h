// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMcpEditorSubsystem;

/**
 * Manages the MCP status indicator in the Unreal Editor status bar.
 * Uses UToolMenus to extend LevelEditor.StatusBar.ToolBar.
 */
class UEBRIDGEMCPEDITOR_API FMcpToolbarExtension
{
public:
	/** Initialize and register the status bar widget */
	static void Initialize();

	/** Cleanup and unregister */
	static void Shutdown();

private:
	/** Register the status bar extension via UToolMenus */
	static void RegisterStatusBarExtension();

	/** Build the status widget */
	static TSharedRef<SWidget> CreateStatusWidget();

	/** Get status icon brush based on server state */
	static const FSlateBrush* GetStatusBrush();

	/** Get status tooltip text */
	static FText GetStatusTooltip();

	/** Get status color */
	static FSlateColor GetStatusColor();

	/** Handle button click */
	static FReply OnStatusButtonClicked();

	/** Get the editor subsystem */
	static UMcpEditorSubsystem* GetSubsystem();

	/** Whether we've been initialized */
	static bool bIsInitialized;
};
