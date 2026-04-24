// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UEBridgeMCP.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUEBridgeMCPEditor, Log, All);
// Note: LogUEBridgeMCP is declared in UEBridgeMCP.h

class FUEBridgeMCPEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Register built-in tools */
	void RegisterBuiltInTools();
};
