#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;

namespace BlueprintCompileUtils
{
	struct FCompileReport
	{
		bool bAttempted = false;
		bool bSuccess = true;
		int32 WarningCount = 0;
		int32 ErrorCount = 0;
		FString ErrorMessage;
		TArray<TSharedPtr<FJsonValue>> Diagnostics;
	};

	bool CompileBlueprintWithReport(
		UBlueprint* Blueprint,
		const FString& AssetPath,
		const FString& SessionId,
		int32 MaxDiagnostics,
		FCompileReport& OutReport);

	TSharedPtr<FJsonObject> MakeCompileReportJson(const FCompileReport& Report);

	bool IsSupportedFixupAction(const FString& Action, bool bAllowConformInterfaces = true);

	bool ApplyFixupAction(
		UBlueprint* Blueprint,
		const FString& Action,
		const FString& AssetPath,
		const FString& SessionId,
		int32 MaxDiagnostics,
		TSharedPtr<FJsonObject>& OutActionResult,
		FString& OutError,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets);
}
