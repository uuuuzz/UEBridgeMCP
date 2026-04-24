#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UClass;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UEnum;
class UFunction;
class UK2Node_EditablePinBase;
class UK2Node_FunctionEntry;
class UK2Node_FunctionResult;
class UScriptStruct;
class FProperty;
struct FEdGraphPinType;
struct FKismetUserDeclaredFunctionMetadata;

namespace BlueprintToolUtils
{
	bool ParseTypeDescriptor(const TSharedPtr<FJsonObject>& TypeObject, FEdGraphPinType& OutPinType, FString& OutError);
	bool ConvertMetadataValueToString(const TSharedPtr<FJsonValue>& JsonValue, FString& OutString);
	FString JsonValueToDefaultString(const TSharedPtr<FJsonValue>& JsonValue, const FEdGraphPinType& PinType);

	bool FindFunctionNodes(
		UEdGraph* Graph,
		UK2Node_FunctionEntry*& OutEntryNode,
		UK2Node_FunctionResult*& OutResultNode,
		FString& OutError);

	bool ApplyFunctionMetadataObject(
		FKismetUserDeclaredFunctionMetadata& FunctionMetaData,
		const TSharedPtr<FJsonObject>& MetadataObject,
		FString& OutError);

	bool SynchronizeUserDefinedPins(
		UK2Node_EditablePinBase* EditableNode,
		const TArray<TSharedPtr<FJsonValue>>* PinDescriptors,
		EEdGraphPinDirection PinDirection,
		FString& OutError);

	UEdGraphPin* FindPinByName(
		UEdGraphNode* Node,
		const FString& PinName,
		EEdGraphPinDirection Direction = EGPD_MAX);

	bool TryParsePosition(
		const TSharedPtr<FJsonObject>& Object,
		const FString& FieldName,
		FVector2D& OutPosition,
		FString& OutError);

	UClass* ResolveClassReference(const FString& ClassPath, UClass* RequiredBaseClass, FString& OutError);
	UClass* ResolveInterfaceClass(const FString& InterfacePath, FString& OutError);
	UScriptStruct* ResolveStruct(const FString& StructPath, FString& OutError);
	UEnum* ResolveEnum(const FString& EnumPath, FString& OutError);
	UFunction* ResolveFunction(UBlueprint* Blueprint, const FString& FunctionClassPath, const FString& FunctionName, FString& OutError);
	FProperty* FindBlueprintProperty(UBlueprint* Blueprint, const FName PropertyName, FString& OutError);
	UEdGraph* ResolveGraph(UBlueprint* Blueprint, const FString& GraphName, FString& OutError);
	UEdGraph* LoadStandardMacroGraph(const FString& MacroName, FString& OutError);

	bool SetPinDefaultLiteral(UEdGraphPin* Pin, const FString& LiteralValue, FString& OutError);

	bool AutoConnectNodes(
		UEdGraphNode* SourceNode,
		UEdGraphNode* TargetNode,
		TArray<TSharedPtr<FJsonValue>>* OutConnections,
		FString& OutError);

	void LayoutNodesSimple(
		const TArray<UEdGraphNode*>& Nodes,
		int32 StartX,
		int32 StartY,
		int32 SpacingX,
		int32 SpacingY);

	FBox2D ComputeNodeBounds(const TArray<UEdGraphNode*>& Nodes, float Padding);
	TArray<TSharedPtr<FJsonValue>> BuildImplementedInterfacesArray(const UBlueprint* Blueprint);
}
