// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"
#include "EditBlueprintMembersTool.generated.h"

class UBlueprint;
class UEdGraph;
class UK2Node_EditablePinBase;
class UK2Node_FunctionEntry;
class UK2Node_FunctionResult;
struct FEdGraphPinType;
struct FKismetUserDeclaredFunctionMetadata;

/**
 * Blueprint member editing tool for variables, functions, and event dispatchers.
 * Supports batched actions with transactional rollback semantics.
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UEditBlueprintMembersTool : public UMcpToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("edit-blueprint-members"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FMcpSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FMcpToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	bool ExecuteAction(
		UBlueprint* Blueprint,
		const TSharedPtr<FJsonObject>& Action,
		int32 Index,
		TSharedPtr<FJsonObject>& OutResult,
		FString& OutError);

	bool CreateVariable(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool RenameVariable(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool DeleteVariable(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetVariableProperties(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool CreateFunction(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool RenameFunction(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool DeleteFunction(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
	bool SetFunctionSignature(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool CreateEventDispatcher(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Action, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	static bool ParseTypeDescriptor(const TSharedPtr<FJsonObject>& TypeObj, FEdGraphPinType& OutPinType, FString& OutError);
	static FString JsonValueToDefaultString(const TSharedPtr<FJsonValue>& JsonValue, const FEdGraphPinType& PinType);
	static bool GetFunctionNodes(UEdGraph* Graph, UK2Node_FunctionEntry*& OutEntryNode, UK2Node_FunctionResult*& OutResultNode, FString& OutError);
	static bool ApplyVariableActionSettings(UBlueprint* Blueprint, const FName VarName, const TSharedPtr<FJsonObject>& Action, bool bAllowTypeChange, FString& OutError);
	static bool ApplyFunctionActionSettings(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Action, bool bAllowOutputs, FString& OutError);
	static bool SynchronizeUserDefinedPins(UK2Node_EditablePinBase* EditableNode, const TArray<TSharedPtr<FJsonValue>>* PinDescriptors, EEdGraphPinDirection PinDirection, FString& OutError);
	static bool ApplyFunctionMetadataObject(FKismetUserDeclaredFunctionMetadata& FunctionMetaData, const TSharedPtr<FJsonObject>& MetadataObject, FString& OutError);
	static void ApplyVariableMetadataObject(UBlueprint* Blueprint, const FName VarName, const TSharedPtr<FJsonObject>& MetadataObject);
	static void SetOrClearBlueprintVariableMetaData(UBlueprint* Blueprint, const FName VarName, const FName MetaDataKey, bool bShouldSet, const FString& EnabledValue = TEXT("true"));
	static void SetOrClearBlueprintVariableMetaData(UBlueprint* Blueprint, const FName VarName, const FName MetaDataKey, const FString& Value);
	static bool ConvertMetadataValueToString(const TSharedPtr<FJsonValue>& JsonValue, FString& OutString);
};
