// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundFrontendLiteral.h"

class FJsonObject;
class FJsonValue;
class UMetaSoundBuilderBase;
class UMetaSoundSource;

namespace MetaSoundToolUtils
{
	FString ObjectPath(const UObject* Object);
	FString BuilderResultToString(EMetaSoundBuilderResult Result);
	FString OutputFormatToString(EMetaSoundOutputAudioFormat Format);

	bool TryResolveOutputFormat(const FString& Name, EMetaSoundOutputAudioFormat& OutFormat, FString& OutError);
	bool TryResolveDataType(const FString& TypeName, FName& OutDataType, FString& OutError);
	bool TryReadLiteral(const FString& TypeName, const TSharedPtr<FJsonValue>& Value, FMetasoundFrontendLiteral& OutLiteral, FString& OutError);

	TSharedPtr<FJsonValue> LiteralToJsonValue(const FMetasoundFrontendLiteral& Literal);
	TSharedPtr<FJsonObject> LiteralToJsonObject(const FMetasoundFrontendLiteral& Literal);

	bool TryLoadSource(const FString& AssetPath, UMetaSoundSource*& OutSource, FString& OutError);
	bool TryBeginBuilding(UMetaSoundSource* Source, UMetaSoundBuilderBase*& OutBuilder, FString& OutError);
	bool BuildExistingSource(UMetaSoundSource* Source, UMetaSoundBuilderBase* Builder, FString& OutError);
	bool SaveAsset(UObject* Asset, TArray<TSharedPtr<FJsonValue>>& OutWarnings);

	TSharedPtr<FJsonObject> SerializeSourceSummary(UMetaSoundSource* Source, bool bIncludeGraph);
	TSharedPtr<FJsonObject> SerializeNodeHandle(const FMetaSoundNodeHandle& Handle);
	TSharedPtr<FJsonObject> SerializeInputHandle(const FMetaSoundBuilderNodeInputHandle& Handle);
	TSharedPtr<FJsonObject> SerializeOutputHandle(const FMetaSoundBuilderNodeOutputHandle& Handle);
}
