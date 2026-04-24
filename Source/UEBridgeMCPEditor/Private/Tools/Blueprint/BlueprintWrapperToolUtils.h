#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Tools/McpToolBase.h"

namespace BlueprintWrapperToolUtils
{
	inline TSharedPtr<FMcpSchemaProperty> MakeOpenObjectSchema(const FString& Description)
	{
		TSharedPtr<FJsonObject> RawSchema = MakeShareable(new FJsonObject);
		RawSchema->SetStringField(TEXT("type"), TEXT("object"));
		RawSchema->SetBoolField(TEXT("additionalProperties"), true);

		TSharedPtr<FMcpSchemaProperty> Schema = MakeShared<FMcpSchemaProperty>();
		Schema->Description = Description;
		Schema->RawSchema = RawSchema;
		return Schema;
	}

	inline TSharedPtr<FMcpSchemaProperty> MakePinArraySchema(const FString& Description)
	{
		TSharedPtr<FMcpSchemaProperty> PinSchema = MakeOpenObjectSchema(TEXT("Blueprint pin descriptor"));
		PinSchema->Type = TEXT("object");

		TSharedPtr<FMcpSchemaProperty> ArraySchema = MakeShared<FMcpSchemaProperty>();
		ArraySchema->Type = TEXT("array");
		ArraySchema->Description = Description;
		ArraySchema->Items = PinSchema;
		return ArraySchema;
	}

	inline TSharedPtr<FMcpSchemaProperty> MakeStringArraySchema(const FString& Description)
	{
		TSharedPtr<FMcpSchemaProperty> ArraySchema = MakeShared<FMcpSchemaProperty>();
		ArraySchema->Type = TEXT("array");
		ArraySchema->ItemsType = TEXT("string");
		ArraySchema->Description = Description;
		return ArraySchema;
	}

	inline TSharedPtr<FMcpSchemaProperty> MakeNumberArraySchema(const FString& Description)
	{
		TSharedPtr<FMcpSchemaProperty> ArraySchema = MakeShared<FMcpSchemaProperty>();
		ArraySchema->Type = TEXT("array");
		ArraySchema->ItemsType = TEXT("number");
		ArraySchema->Description = Description;
		return ArraySchema;
	}

	inline void CopyFieldIfPresent(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target, const FString& FieldName)
	{
		if (const TSharedPtr<FJsonValue>* ValuePtr = Source->Values.Find(FieldName))
		{
			Target->SetField(FieldName, *ValuePtr);
		}
	}

	inline bool CompilePolicyToGraphBool(const TSharedPtr<FJsonObject>& Arguments, bool bDefaultValue = true)
	{
		FString CompilePolicy;
		if (!Arguments->TryGetStringField(TEXT("compile"), CompilePolicy))
		{
			return bDefaultValue;
		}

		return !CompilePolicy.Equals(TEXT("never"), ESearchCase::IgnoreCase);
	}
}
