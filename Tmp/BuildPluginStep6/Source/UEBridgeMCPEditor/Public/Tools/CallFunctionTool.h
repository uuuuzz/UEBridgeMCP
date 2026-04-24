// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "CallFunctionTool.generated.h"

/**
 * Call functions on actors, components, or global Blueprint libraries.
 * Supports editor and PIE worlds.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UCallFunctionTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("call-function"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("target")}; }
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	UActorComponent* FindComponentByName(AActor* Actor, const FString& ComponentName) const;

	FMcpToolResult CallFunctionOnObject(UObject* Object, const FString& FunctionName, const TSharedPtr<FJsonObject>& Arguments);
	FMcpToolResult CallGlobalFunction(const FString& BlueprintPath, const FString& FunctionName, const TSharedPtr<FJsonObject>& Arguments);

	TSharedPtr<FJsonValue> GetPropertyValue(void* Container, FProperty* Property) const;
	bool SetPropertyFromJson(void* PropertyAddr, FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue) const;
};
