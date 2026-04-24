// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;

namespace PerformanceToolUtils
{
	UWorld* ResolveWorld(const FString& RequestedWorldType);
	TSharedPtr<FJsonObject> BuildPerformanceReport(UWorld* World, const FString& RequestedWorldType);
}
