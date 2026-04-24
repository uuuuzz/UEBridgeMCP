// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class FJsonObject;

class UEBRIDGEMCPEDITOR_API FMcpOptionalCapabilityUtils
{
public:
	static bool IsSequencerAvailable();
	static bool IsLandscapeAvailable();
	static bool IsFoliageAvailable();
	static bool IsWorldPartitionAvailable();
	static bool IsControlRigAvailable();
	static bool IsPCGAvailable();
	static bool IsExternalAIAvailable();

	static TSharedPtr<FJsonObject> BuildOptionalCapabilities(UWorld* World = nullptr);

private:
	static bool IsPluginEnabled(const TCHAR* PluginName);
	static bool HasModule(const TCHAR* ModuleName);
};
