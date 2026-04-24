// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Plugin version - keep in sync with UEBridgeMCP.uplugin */
#define UEBRIDGEMCP_VERSION TEXT("1.19.0")

DECLARE_LOG_CATEGORY_EXTERN(LogUEBridgeMCP, Log, All);

class FUEBridgeMCPModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
