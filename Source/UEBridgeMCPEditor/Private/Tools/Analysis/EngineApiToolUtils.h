// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProperty;
class IPlugin;
class UClass;
class UFunction;

namespace EngineApiToolUtils
{
	struct FScoredJson
	{
		double Score = 0.0;
		TSharedPtr<FJsonObject> Object;
	};

	UClass* ResolveClass(const FString& ClassName, FString& OutError);
	FString ClassKind(UClass* Class);
	FString ClassFlagsToString(UClass* Class);
	FString FunctionFlagsToString(UFunction* Function);
	FString PropertyFlagsToString(FProperty* Property);
	FString PropertyTypeToString(FProperty* Property);

	TSharedPtr<FJsonObject> SerializeClass(UClass* Class, double Score = 0.0);
	TSharedPtr<FJsonObject> SerializeFunction(UFunction* Function, UClass* OwnerClass, bool bIncludeMetadata, double Score = 0.0);
	TSharedPtr<FJsonObject> SerializeProperty(FProperty* Property, UClass* OwnerClass, bool bIncludeMetadata, double Score = 0.0);
	TSharedPtr<FJsonObject> SerializePlugin(const TSharedRef<IPlugin>& Plugin, bool bIncludeModules, bool bIncludePaths, double Score = 0.0);

	void SortAndTrim(TArray<FScoredJson>& Items, int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutArray);
	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues);
	bool TypeSetAllows(const TSet<FString>& Types, const FString& Type);
	TSet<FString> ReadLowercaseStringSet(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName);
}
