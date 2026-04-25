#include "Tools/Blueprint/BlueprintToolUtils.h"

#include "Tools/Write/AddGraphNodeTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "Utils/McpTypeDescriptorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Field.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	bool FindFunctionResultNode(UEdGraph* Graph, UK2Node_FunctionResult*& OutResultNode)
	{
		OutResultNode = nullptr;
		if (!Graph)
		{
			return false;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				OutResultNode = ResultNode;
				return true;
			}
		}

		return false;
	}

	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	bool IsCandidatePin(const UEdGraphPin* Pin)
	{
		return Pin
			&& !Pin->bHidden
			&& !Pin->bNotConnectable
			&& Pin->Direction != EGPD_MAX;
	}

	FString GetStableNodeSortKey(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FString();
		}

		const FString GuidString = Node->NodeGuid.IsValid()
			? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)
			: TEXT("00000000-0000-0000-0000-000000000000");
		return FString::Printf(TEXT("%08d_%08d_%s"), Node->NodePosX, Node->NodePosY, *GuidString);
	}
}

namespace BlueprintToolUtils
{
	bool ParseTypeDescriptor(const TSharedPtr<FJsonObject>& TypeObject, FEdGraphPinType& OutPinType, FString& OutError)
	{
		return McpTypeDescriptorUtils::ParseTypeDescriptor(TypeObject, OutPinType, OutError);
	}

	bool ConvertMetadataValueToString(const TSharedPtr<FJsonValue>& JsonValue, FString& OutString)
	{
		if (!JsonValue.IsValid() || JsonValue->Type == EJson::Null)
		{
			OutString.Reset();
			return true;
		}

		if (JsonValue->TryGetString(OutString))
		{
			return true;
		}

		bool bBooleanValue = false;
		if (JsonValue->TryGetBool(bBooleanValue))
		{
			OutString = bBooleanValue ? TEXT("true") : TEXT("false");
			return true;
		}

		double NumberValue = 0.0;
		if (JsonValue->TryGetNumber(NumberValue))
		{
			OutString = FString::SanitizeFloat(NumberValue);
			return true;
		}

		FString SerializedValue;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedValue);
		if (FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT(""), Writer))
		{
			Writer->Close();
			OutString = MoveTemp(SerializedValue);
			return true;
		}

		return false;
	}

	FString JsonValueToDefaultString(const TSharedPtr<FJsonValue>& JsonValue, const FEdGraphPinType& PinType)
	{
		if (!JsonValue.IsValid() || JsonValue->Type == EJson::Null)
		{
			return FString();
		}

		const FString Category = PinType.PinCategory.ToString();

		if (Category == UEdGraphSchema_K2::PC_Boolean)
		{
			bool bVal = false;
			if (JsonValue->TryGetBool(bVal))
			{
				return bVal ? TEXT("true") : TEXT("false");
			}

			FString StrVal;
			if (JsonValue->TryGetString(StrVal))
			{
				return StrVal.ToLower();
			}
			return TEXT("false");
		}

		if (Category == UEdGraphSchema_K2::PC_Int
			|| Category == UEdGraphSchema_K2::PC_Int64
			|| Category == UEdGraphSchema_K2::PC_Byte
			|| Category == UEdGraphSchema_K2::PC_Real)
		{
			double NumVal = 0.0;
			if (JsonValue->TryGetNumber(NumVal))
			{
				if (Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Byte)
				{
					return FString::Printf(TEXT("%d"), FMath::RoundToInt32(NumVal));
				}
				if (Category == UEdGraphSchema_K2::PC_Int64)
				{
					return FString::Printf(TEXT("%lld"), static_cast<int64>(NumVal));
				}
				return FString::SanitizeFloat(NumVal);
			}

			FString StrVal;
			if (JsonValue->TryGetString(StrVal))
			{
				return StrVal;
			}
			return TEXT("0");
		}

		if (Category == UEdGraphSchema_K2::PC_String
			|| Category == UEdGraphSchema_K2::PC_Name
			|| Category == UEdGraphSchema_K2::PC_Text)
		{
			FString StrVal;
			if (JsonValue->TryGetString(StrVal))
			{
				return StrVal;
			}

			double NumVal = 0.0;
			if (JsonValue->TryGetNumber(NumVal))
			{
				return FString::SanitizeFloat(NumVal);
			}

			bool bVal = false;
			if (JsonValue->TryGetBool(bVal))
			{
				return bVal ? TEXT("true") : TEXT("false");
			}

			return FString();
		}

		if (Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_SoftClass
			|| Category == UEdGraphSchema_K2::PC_Enum
			|| Category == UEdGraphSchema_K2::PC_Struct)
		{
			FString StrVal;
			if (JsonValue->TryGetString(StrVal))
			{
				return StrVal;
			}

			FString Serialized;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
			if (FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT(""), Writer))
			{
				Writer->Close();
				return Serialized;
			}
		}

		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal;
		}

		return FString();
	}

	bool FindFunctionNodes(
		UEdGraph* Graph,
		UK2Node_FunctionEntry*& OutEntryNode,
		UK2Node_FunctionResult*& OutResultNode,
		FString& OutError)
	{
		OutEntryNode = nullptr;
		OutResultNode = nullptr;

		if (!Graph)
		{
			OutError = TEXT("Function graph is null");
			return false;
		}

		OutEntryNode = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph));
		FindFunctionResultNode(Graph, OutResultNode);
		if (!OutEntryNode)
		{
			OutError = FString::Printf(TEXT("Function entry node not found in '%s'"), *Graph->GetName());
			return false;
		}

		return true;
	}

	bool ApplyFunctionMetadataObject(
		FKismetUserDeclaredFunctionMetadata& FunctionMetaData,
		const TSharedPtr<FJsonObject>& MetadataObject,
		FString& OutError)
	{
		if (!MetadataObject.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : MetadataObject->Values)
		{
			const FString& Key = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			if (Key.Equals(TEXT("ToolTip"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("Tooltip"), ESearchCase::IgnoreCase))
			{
				FString MetadataValue;
				if (!ConvertMetadataValueToString(Value, MetadataValue))
				{
					OutError = FString::Printf(TEXT("Could not convert function metadata value '%s' to string"), *Key);
					return false;
				}
				FunctionMetaData.ToolTip = FText::FromString(MetadataValue);
				continue;
			}

			if (Key.Equals(TEXT("Category"), ESearchCase::IgnoreCase))
			{
				FString MetadataValue;
				if (!ConvertMetadataValueToString(Value, MetadataValue))
				{
					OutError = FString::Printf(TEXT("Could not convert function metadata value '%s' to string"), *Key);
					return false;
				}
				FunctionMetaData.Category = FText::FromString(MetadataValue);
				continue;
			}

			if (Key.Equals(TEXT("Keywords"), ESearchCase::IgnoreCase))
			{
				FString MetadataValue;
				if (!ConvertMetadataValueToString(Value, MetadataValue))
				{
					OutError = FString::Printf(TEXT("Could not convert function metadata value '%s' to string"), *Key);
					return false;
				}
				FunctionMetaData.Keywords = FText::FromString(MetadataValue);
				continue;
			}

			if (Key.Equals(TEXT("CompactNodeTitle"), ESearchCase::IgnoreCase))
			{
				FString MetadataValue;
				if (!ConvertMetadataValueToString(Value, MetadataValue))
				{
					OutError = FString::Printf(TEXT("Could not convert function metadata value '%s' to string"), *Key);
					return false;
				}
				FunctionMetaData.CompactNodeTitle = FText::FromString(MetadataValue);
				continue;
			}

			if (Key.Equals(TEXT("DeprecationMessage"), ESearchCase::IgnoreCase))
			{
				FString MetadataValue;
				if (!ConvertMetadataValueToString(Value, MetadataValue))
				{
					OutError = FString::Printf(TEXT("Could not convert function metadata value '%s' to string"), *Key);
					return false;
				}
				FunctionMetaData.DeprecationMessage = MetadataValue;
				continue;
			}

			if (Key.Equals(TEXT("CallInEditor"), ESearchCase::IgnoreCase))
			{
				bool bFlagValue = false;
				if (!Value.IsValid() || !Value->TryGetBool(bFlagValue))
				{
					OutError = TEXT("Function metadata 'CallInEditor' must be a boolean");
					return false;
				}
				FunctionMetaData.bCallInEditor = bFlagValue;
				continue;
			}

			if (Key.Equals(TEXT("DeprecatedFunction"), ESearchCase::IgnoreCase))
			{
				bool bFlagValue = false;
				if (!Value.IsValid() || !Value->TryGetBool(bFlagValue))
				{
					OutError = TEXT("Function metadata 'DeprecatedFunction' must be a boolean");
					return false;
				}
				FunctionMetaData.bIsDeprecated = bFlagValue;
				continue;
			}

			if (Key.Equals(TEXT("ThreadSafe"), ESearchCase::IgnoreCase))
			{
				bool bFlagValue = false;
				if (!Value.IsValid() || !Value->TryGetBool(bFlagValue))
				{
					OutError = TEXT("Function metadata 'ThreadSafe' must be a boolean");
					return false;
				}
				FunctionMetaData.bThreadSafe = bFlagValue;
				continue;
			}

			if (Key.Equals(TEXT("UnsafeForConstructionScripts"), ESearchCase::IgnoreCase))
			{
				bool bFlagValue = false;
				if (!Value.IsValid() || !Value->TryGetBool(bFlagValue))
				{
					OutError = TEXT("Function metadata 'UnsafeForConstructionScripts' must be a boolean");
					return false;
				}
				FunctionMetaData.bIsUnsafeDuringActorConstruction = bFlagValue;
				continue;
			}

			FString MetadataValue;
			if (!ConvertMetadataValueToString(Value, MetadataValue))
			{
				OutError = FString::Printf(TEXT("Could not convert function metadata value '%s' to string"), *Key);
				return false;
			}
			FunctionMetaData.SetMetaData(FName(*Key), MoveTemp(MetadataValue));
		}

		return true;
	}

	bool SynchronizeUserDefinedPins(
		UK2Node_EditablePinBase* EditableNode,
		const TArray<TSharedPtr<FJsonValue>>* PinDescriptors,
		EEdGraphPinDirection PinDirection,
		FString& OutError)
	{
		if (!EditableNode)
		{
			OutError = TEXT("Editable pin node is null");
			return false;
		}

		const TArray<TSharedPtr<FUserPinInfo>> ExistingPins = EditableNode->UserDefinedPins;
		for (const TSharedPtr<FUserPinInfo>& ExistingPin : ExistingPins)
		{
			EditableNode->RemoveUserDefinedPin(ExistingPin);
		}

		if (!PinDescriptors)
		{
			return true;
		}

		TSet<FName> SeenPins;
		for (const TSharedPtr<FJsonValue>& PinValue : *PinDescriptors)
		{
			const TSharedPtr<FJsonObject>* PinObject = nullptr;
			if (!PinValue.IsValid() || !PinValue->TryGetObject(PinObject) || !PinObject || !(*PinObject).IsValid())
			{
				OutError = TEXT("Pin descriptor must be an object");
				return false;
			}

			FString PinNameString;
			if (!(*PinObject)->TryGetStringField(TEXT("name"), PinNameString) || PinNameString.IsEmpty())
			{
				OutError = TEXT("Pin descriptor requires a non-empty 'name'");
				return false;
			}

			const FName PinName(*PinNameString);
			if (SeenPins.Contains(PinName))
			{
				OutError = FString::Printf(TEXT("Duplicate pin name '%s'"), *PinNameString);
				return false;
			}
			SeenPins.Add(PinName);

			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;

			const TSharedPtr<FJsonObject>* TypeObject = nullptr;
			if ((*PinObject)->TryGetObjectField(TEXT("type"), TypeObject) && TypeObject && (*TypeObject).IsValid())
			{
				if (!ParseTypeDescriptor(*TypeObject, PinType, OutError))
				{
					return false;
				}
			}

			bool bPassByReference = false;
			if ((*PinObject)->TryGetBoolField(TEXT("pass_by_reference"), bPassByReference))
			{
				PinType.bIsReference = bPassByReference;
			}

			bool bIsConst = false;
			if ((*PinObject)->TryGetBoolField(TEXT("is_const"), bIsConst))
			{
				PinType.bIsConst = bIsConst;
			}

			UEdGraphPin* CreatedPin = EditableNode->CreateUserDefinedPin(PinName, PinType, PinDirection, false);
			if (!CreatedPin)
			{
				OutError = FString::Printf(TEXT("Failed to create pin '%s'"), *PinNameString);
				return false;
			}

			TSharedPtr<FUserPinInfo> CreatedPinInfo;
			for (const TSharedPtr<FUserPinInfo>& PinInfo : EditableNode->UserDefinedPins)
			{
				if (PinInfo.IsValid() && PinInfo->PinName == CreatedPin->PinName)
				{
					CreatedPinInfo = PinInfo;
					break;
				}
			}

			if (!CreatedPinInfo.IsValid())
			{
				OutError = FString::Printf(TEXT("Failed to locate created pin info for '%s'"), *PinNameString);
				return false;
			}

			if ((*PinObject)->HasField(TEXT("default_value")))
			{
				const FString DefaultValueString = JsonValueToDefaultString((*PinObject)->TryGetField(TEXT("default_value")), PinType);
				if (!EditableNode->ModifyUserDefinedPinDefaultValue(CreatedPinInfo, DefaultValueString))
				{
					CreatedPinInfo->PinDefaultValue = DefaultValueString;
					CreatedPin->DefaultValue = DefaultValueString;
				}
			}

			FString Description;
			if ((*PinObject)->TryGetStringField(TEXT("description"), Description))
			{
				CreatedPin->PinToolTip = Description;
			}
		}

		return true;
	}

	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			if (!Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (Direction != EGPD_MAX && Pin->Direction != Direction)
			{
				continue;
			}
			return Pin;
		}

		return nullptr;
	}

	bool TryParsePosition(
		const TSharedPtr<FJsonObject>& Object,
		const FString& FieldName,
		FVector2D& OutPosition,
		FString& OutError)
	{
		OutPosition = FVector2D::ZeroVector;

		const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, PositionArray) || !PositionArray || PositionArray->Num() < 2)
		{
			OutError = FString::Printf(TEXT("'%s' must be [x, y]"), *FieldName);
			return false;
		}

		OutPosition.X = static_cast<float>((*PositionArray)[0]->AsNumber());
		OutPosition.Y = static_cast<float>((*PositionArray)[1]->AsNumber());
		return true;
	}

	UClass* ResolveClassReference(const FString& ClassPath, UClass* RequiredBaseClass, FString& OutError)
	{
		if (ClassPath.IsEmpty())
		{
			OutError = TEXT("Class path is empty");
			return nullptr;
		}

		UClass* ResolvedClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!ResolvedClass)
		{
			FString ResolveError;
			ResolvedClass = FMcpPropertySerializer::ResolveClass(ClassPath, ResolveError);
		}

		if (!ResolvedClass)
		{
			if (UBlueprint* BlueprintAsset = LoadObject<UBlueprint>(nullptr, *ClassPath))
			{
				ResolvedClass = BlueprintAsset->GeneratedClass ? BlueprintAsset->GeneratedClass : BlueprintAsset->SkeletonGeneratedClass;
			}
		}

		if (!ResolvedClass)
		{
			OutError = FString::Printf(TEXT("Could not resolve class '%s'"), *ClassPath);
			return nullptr;
		}

		if (RequiredBaseClass && !ResolvedClass->IsChildOf(RequiredBaseClass))
		{
			OutError = FString::Printf(TEXT("Class '%s' is not a %s"), *ClassPath, *RequiredBaseClass->GetName());
			return nullptr;
		}

		return ResolvedClass;
	}

	UClass* ResolveInterfaceClass(const FString& InterfacePath, FString& OutError)
	{
		UClass* InterfaceClass = ResolveClassReference(InterfacePath, UInterface::StaticClass(), OutError);
		if (!InterfaceClass)
		{
			return nullptr;
		}

		if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
		{
			OutError = FString::Printf(TEXT("Class '%s' is not an interface"), *InterfacePath);
			return nullptr;
		}

		return InterfaceClass;
	}

	UScriptStruct* ResolveStruct(const FString& StructPath, FString& OutError)
	{
		if (StructPath.IsEmpty())
		{
			OutError = TEXT("Struct path is empty");
			return nullptr;
		}

		if (UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *StructPath))
		{
			return Struct;
		}

		OutError = FString::Printf(TEXT("Could not resolve struct '%s'"), *StructPath);
		return nullptr;
	}

	UEnum* ResolveEnum(const FString& EnumPath, FString& OutError)
	{
		if (EnumPath.IsEmpty())
		{
			OutError = TEXT("Enum path is empty");
			return nullptr;
		}

		if (UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath))
		{
			return Enum;
		}

		OutError = FString::Printf(TEXT("Could not resolve enum '%s'"), *EnumPath);
		return nullptr;
	}

	UFunction* ResolveFunction(UBlueprint* Blueprint, const FString& FunctionClassPath, const FString& FunctionName, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null");
			return nullptr;
		}

		if (FunctionName.IsEmpty())
		{
			OutError = TEXT("Function name is empty");
			return nullptr;
		}

		const FName FunctionFName(*FunctionName);
		auto FindFunctionOnClass = [&](UClass* Class) -> UFunction*
		{
			return Class ? Class->FindFunctionByName(FunctionFName, EIncludeSuperFlag::IncludeSuper) : nullptr;
		};

		if (!FunctionClassPath.IsEmpty())
		{
			UClass* OwnerClass = ResolveClassReference(FunctionClassPath, UObject::StaticClass(), OutError);
			return OwnerClass ? FindFunctionOnClass(OwnerClass) : nullptr;
		}

		if (UFunction* Function = FindFunctionOnClass(Blueprint->SkeletonGeneratedClass))
		{
			return Function;
		}

		if (UFunction* Function = FindFunctionOnClass(Blueprint->GeneratedClass))
		{
			return Function;
		}

		if (UFunction* Function = FindFunctionOnClass(Blueprint->ParentClass))
		{
			return Function;
		}

		OutError = FString::Printf(TEXT("Function '%s' not found on Blueprint self/parent classes"), *FunctionName);
		return nullptr;
	}

	FProperty* FindBlueprintProperty(UBlueprint* Blueprint, const FName PropertyName, FString& OutError)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null");
			return nullptr;
		}

		auto FindPropertyOnClass = [&](UClass* Class) -> FProperty*
		{
			return Class ? Class->FindPropertyByName(PropertyName) : nullptr;
		};

		if (FProperty* Property = FindPropertyOnClass(Blueprint->SkeletonGeneratedClass))
		{
			return Property;
		}

		if (FProperty* Property = FindPropertyOnClass(Blueprint->GeneratedClass))
		{
			return Property;
		}

		if (FProperty* Property = FindPropertyOnClass(Blueprint->ParentClass))
		{
			return Property;
		}

		OutError = FString::Printf(TEXT("Property '%s' not found"), *PropertyName.ToString());
		return nullptr;
	}

	UEdGraph* ResolveGraph(UBlueprint* Blueprint, const FString& GraphName, FString& OutError)
	{
		const FString EffectiveGraphName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;
		UEdGraph* Graph = FMcpAssetModifier::FindGraphByName(Blueprint, EffectiveGraphName);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("Graph not found: %s"), *EffectiveGraphName);
		}
		return Graph;
	}

	UEdGraph* LoadStandardMacroGraph(const FString& MacroName, FString& OutError)
	{
		UBlueprint* StandardMacros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
		if (!StandardMacros)
		{
			OutError = TEXT("Could not load /Engine/EditorBlueprintResources/StandardMacros.StandardMacros");
			return nullptr;
		}

		UEdGraph* MacroGraph = FMcpAssetModifier::FindGraphByName(StandardMacros, MacroName);
		if (!MacroGraph)
		{
			OutError = FString::Printf(TEXT("Standard macro '%s' not found"), *MacroName);
		}
		return MacroGraph;
	}

	bool SetPinDefaultLiteral(UEdGraphPin* Pin, const FString& LiteralValue, FString& OutError)
	{
		if (!Pin)
		{
			OutError = TEXT("Pin is null");
			return false;
		}

		const UEdGraphSchema* Schema = Pin->GetSchema();
		const FName PinCategory = Pin->PinType.PinCategory;

		if (PinCategory == UEdGraphSchema_K2::PC_Object || PinCategory == UEdGraphSchema_K2::PC_SoftObject)
		{
			if (LiteralValue.IsEmpty() || LiteralValue.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				Pin->DefaultObject = nullptr;
				Pin->DefaultValue.Reset();
				return true;
			}

			UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *LiteralValue);
			if (!Asset)
			{
				OutError = FString::Printf(TEXT("Could not load object '%s' for pin '%s'"), *LiteralValue, *Pin->PinName.ToString());
				return false;
			}

			Pin->DefaultObject = Asset;
			Pin->DefaultValue.Reset();
			return true;
		}

		if (PinCategory == UEdGraphSchema_K2::PC_Class || PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			if (LiteralValue.IsEmpty() || LiteralValue.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				Pin->DefaultObject = nullptr;
				Pin->DefaultValue.Reset();
				return true;
			}

			FString ClassError;
			UClass* ClassObject = ResolveClassReference(LiteralValue, UObject::StaticClass(), ClassError);
			if (!ClassObject)
			{
				OutError = ClassError;
				return false;
			}

			Pin->DefaultObject = ClassObject;
			Pin->DefaultValue.Reset();
			return true;
		}

		if (Schema)
		{
			Schema->TrySetDefaultValue(*Pin, LiteralValue);
			return true;
		}

		Pin->DefaultValue = LiteralValue;
		return true;
	}

	bool AutoConnectNodes(
		UEdGraphNode* SourceNode,
		UEdGraphNode* TargetNode,
		TArray<TSharedPtr<FJsonValue>>* OutConnections,
		FString& OutError)
	{
		if (!SourceNode || !TargetNode)
		{
			OutError = TEXT("Source and target nodes are required");
			return false;
		}

		const UEdGraphSchema* Schema = SourceNode->GetGraph() ? SourceNode->GetGraph()->GetSchema() : nullptr;
		if (!Schema)
		{
			OutError = TEXT("Graph schema not available");
			return false;
		}

		auto RecordConnection = [&](UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
		{
			if (!OutConnections || !SourcePin || !TargetPin)
			{
				return;
			}

			TSharedPtr<FJsonObject> ConnectionObject = MakeShareable(new FJsonObject);
			ConnectionObject->SetStringField(TEXT("source_pin"), SourcePin->PinName.ToString());
			ConnectionObject->SetStringField(TEXT("target_pin"), TargetPin->PinName.ToString());
			OutConnections->Add(MakeShareable(new FJsonValueObject(ConnectionObject)));
		};

		int32 ConnectedCount = 0;

		for (UEdGraphPin* SourcePin : SourceNode->Pins)
		{
			if (!IsCandidatePin(SourcePin) || SourcePin->Direction != EGPD_Output || !IsExecPin(SourcePin))
			{
				continue;
			}

			for (UEdGraphPin* TargetPin : TargetNode->Pins)
			{
				if (!IsCandidatePin(TargetPin) || TargetPin->Direction != EGPD_Input || !IsExecPin(TargetPin))
				{
					continue;
				}

				FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
				if (Response.Response == CONNECT_RESPONSE_DISALLOW)
				{
					continue;
				}

				if (Schema->TryCreateConnection(SourcePin, TargetPin))
				{
					RecordConnection(SourcePin, TargetPin);
					++ConnectedCount;
					break;
				}
			}
		}

		for (UEdGraphPin* SourcePin : SourceNode->Pins)
		{
			if (!IsCandidatePin(SourcePin)
				|| SourcePin->Direction != EGPD_Output
				|| IsExecPin(SourcePin)
				|| SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
			{
				continue;
			}

			UEdGraphPin* BestTargetPin = nullptr;

			for (UEdGraphPin* TargetPin : TargetNode->Pins)
			{
				if (!IsCandidatePin(TargetPin)
					|| TargetPin->Direction != EGPD_Input
					|| IsExecPin(TargetPin)
					|| TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
					|| TargetPin->LinkedTo.Num() > 0)
				{
					continue;
				}

				FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
				if (Response.Response == CONNECT_RESPONSE_DISALLOW)
				{
					continue;
				}

				if (TargetPin->PinName == SourcePin->PinName)
				{
					BestTargetPin = TargetPin;
					break;
				}

				if (!BestTargetPin
					&& TargetPin->PinType.PinCategory == SourcePin->PinType.PinCategory
					&& TargetPin->PinType.PinSubCategory == SourcePin->PinType.PinSubCategory
					&& TargetPin->PinType.PinSubCategoryObject == SourcePin->PinType.PinSubCategoryObject)
				{
					BestTargetPin = TargetPin;
				}
			}

			if (BestTargetPin && Schema->TryCreateConnection(SourcePin, BestTargetPin))
			{
				RecordConnection(SourcePin, BestTargetPin);
				++ConnectedCount;
			}
		}

		if (ConnectedCount == 0)
		{
			OutError = TEXT("No compatible pins found to auto-connect");
			return false;
		}

		return true;
	}

	void LayoutNodesSimple(
		const TArray<UEdGraphNode*>& Nodes,
		int32 StartX,
		int32 StartY,
		int32 SpacingX,
		int32 SpacingY)
	{
		if (Nodes.Num() == 0)
		{
			return;
		}

		TMap<UEdGraphNode*, int32> IncomingExecCount;
		TMap<UEdGraphNode*, int32> Depths;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				IncomingExecCount.Add(Node, 0);
				Depths.Add(Node, 0);
			}
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (!Node)
			{
				continue;
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode() || !IncomingExecCount.Contains(LinkedPin->GetOwningNode()))
					{
						continue;
					}

					IncomingExecCount.FindOrAdd(LinkedPin->GetOwningNode()) += 1;
				}
			}
		}

		TArray<UEdGraphNode*> SortedRoots;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node && IncomingExecCount.FindRef(Node) == 0)
			{
				SortedRoots.Add(Node);
			}
		}

		SortedRoots.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return GetStableNodeSortKey(&A) < GetStableNodeSortKey(&B);
		});

		TSet<UEdGraphNode*> Visited;
		TQueue<UEdGraphNode*> Queue;

		auto EnqueueRoot = [&](UEdGraphNode* RootNode, int32 RootDepth)
		{
			if (!RootNode || Visited.Contains(RootNode))
			{
				return;
			}

			Depths.FindOrAdd(RootNode) = RootDepth;
			Visited.Add(RootNode);
			Queue.Enqueue(RootNode);

			while (!Queue.IsEmpty())
			{
				UEdGraphNode* CurrentNode = nullptr;
				Queue.Dequeue(CurrentNode);
				const int32 CurrentDepth = Depths.FindRef(CurrentNode);

				for (UEdGraphPin* Pin : CurrentNode->Pins)
				{
					if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output)
					{
						continue;
					}

					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UEdGraphNode* NextNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
						if (!NextNode || !Depths.Contains(NextNode))
						{
							continue;
						}

						const int32 NextDepth = FMath::Max(Depths.FindRef(NextNode), CurrentDepth + 1);
						Depths.FindOrAdd(NextNode) = NextDepth;
						if (!Visited.Contains(NextNode))
						{
							Visited.Add(NextNode);
							Queue.Enqueue(NextNode);
						}
					}
				}
			}
		};

		for (UEdGraphNode* RootNode : SortedRoots)
		{
			EnqueueRoot(RootNode, 0);
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (Node && !Visited.Contains(Node))
			{
				EnqueueRoot(Node, 0);
			}
		}

		TMap<int32, TArray<UEdGraphNode*>> Columns;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Columns.FindOrAdd(Depths.FindRef(Node)).Add(Node);
			}
		}

		for (TPair<int32, TArray<UEdGraphNode*>>& Pair : Columns)
		{
			Pair.Value.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return GetStableNodeSortKey(&A) < GetStableNodeSortKey(&B);
			});
		}

		for (TPair<int32, TArray<UEdGraphNode*>>& Pair : Columns)
		{
			const int32 ColumnIndex = Pair.Key;
			for (int32 RowIndex = 0; RowIndex < Pair.Value.Num(); ++RowIndex)
			{
				if (UEdGraphNode* Node = Pair.Value[RowIndex])
				{
					Node->NodePosX = StartX + (ColumnIndex * SpacingX);
					Node->NodePosY = StartY + (RowIndex * SpacingY);
				}
			}
		}
	}

	void LayoutNodesByMeasuredSize(
		const TArray<UEdGraphNode*>& Nodes,
		const TMap<UEdGraphNode*, FVector2D>& NodeSizes,
		int32 StartX,
		int32 StartY,
		int32 PaddingX,
		int32 PaddingY)
	{
		if (Nodes.Num() == 0)
		{
			return;
		}

		TMap<UEdGraphNode*, int32> IncomingExecCount;
		TMap<UEdGraphNode*, int32> Depths;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				IncomingExecCount.Add(Node, 0);
				Depths.Add(Node, 0);
			}
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (!Node)
			{
				continue;
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					if (LinkedNode && IncomingExecCount.Contains(LinkedNode))
					{
						IncomingExecCount.FindOrAdd(LinkedNode) += 1;
					}
				}
			}
		}

		TArray<UEdGraphNode*> SortedRoots;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node && IncomingExecCount.FindRef(Node) == 0)
			{
				SortedRoots.Add(Node);
			}
		}

		SortedRoots.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return GetStableNodeSortKey(&A) < GetStableNodeSortKey(&B);
		});

		TSet<UEdGraphNode*> Visited;
		TQueue<UEdGraphNode*> Queue;

		auto EnqueueRoot = [&](UEdGraphNode* RootNode, int32 RootDepth)
		{
			if (!RootNode || Visited.Contains(RootNode))
			{
				return;
			}

			Depths.FindOrAdd(RootNode) = RootDepth;
			Visited.Add(RootNode);
			Queue.Enqueue(RootNode);

			while (!Queue.IsEmpty())
			{
				UEdGraphNode* CurrentNode = nullptr;
				Queue.Dequeue(CurrentNode);
				const int32 CurrentDepth = Depths.FindRef(CurrentNode);

				for (UEdGraphPin* Pin : CurrentNode->Pins)
				{
					if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output)
					{
						continue;
					}

					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						UEdGraphNode* NextNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
						if (!NextNode || !Depths.Contains(NextNode))
						{
							continue;
						}

						const int32 NextDepth = FMath::Max(Depths.FindRef(NextNode), CurrentDepth + 1);
						Depths.FindOrAdd(NextNode) = NextDepth;
						if (!Visited.Contains(NextNode))
						{
							Visited.Add(NextNode);
							Queue.Enqueue(NextNode);
						}
					}
				}
			}
		};

		for (UEdGraphNode* RootNode : SortedRoots)
		{
			EnqueueRoot(RootNode, 0);
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (Node && !Visited.Contains(Node))
			{
				EnqueueRoot(Node, 0);
			}
		}

		TMap<int32, TArray<UEdGraphNode*>> Columns;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Columns.FindOrAdd(Depths.FindRef(Node)).Add(Node);
			}
		}

		TArray<int32> ColumnKeys;
		Columns.GenerateKeyArray(ColumnKeys);
		ColumnKeys.Sort();

		TMap<int32, float> ColumnWidths;
		for (int32 ColumnKey : ColumnKeys)
		{
			TArray<UEdGraphNode*>& ColumnNodes = Columns.FindChecked(ColumnKey);
			ColumnNodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return GetStableNodeSortKey(&A) < GetStableNodeSortKey(&B);
			});

			float MaxWidth = 240.0f;
			for (UEdGraphNode* Node : ColumnNodes)
			{
				const FVector2D Size = NodeSizes.FindRef(Node);
				MaxWidth = FMath::Max(MaxWidth, static_cast<float>(Size.X));
			}
			ColumnWidths.Add(ColumnKey, MaxWidth);
		}

		TMap<int32, float> ColumnX;
		float CursorX = static_cast<float>(StartX);
		for (int32 ColumnKey : ColumnKeys)
		{
			ColumnX.Add(ColumnKey, CursorX);
			CursorX += ColumnWidths.FindRef(ColumnKey) + FMath::Max(0, PaddingX);
		}

		for (int32 ColumnKey : ColumnKeys)
		{
			float CursorY = static_cast<float>(StartY);
			for (UEdGraphNode* Node : Columns.FindChecked(ColumnKey))
			{
				if (!Node)
				{
					continue;
				}

				const FVector2D Size = NodeSizes.FindRef(Node);
				Node->NodePosX = FMath::RoundToInt(ColumnX.FindRef(ColumnKey));
				Node->NodePosY = FMath::RoundToInt(CursorY);
				CursorY += FMath::Max(120.0f, static_cast<float>(Size.Y)) + FMath::Max(0, PaddingY);
			}
		}
	}

	FBox2D ComputeNodeBounds(const TArray<UEdGraphNode*>& Nodes, float Padding)
	{
		bool bHasPoint = false;
		FBox2D Bounds(EForceInit::ForceInit);

		for (UEdGraphNode* Node : Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FVector2D Min(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
			const FVector2D Max(
				static_cast<float>(Node->NodePosX) + FMath::Max(240.0f, Node->NodeWidth),
				static_cast<float>(Node->NodePosY) + FMath::Max(120.0f, Node->NodeHeight));

			if (!bHasPoint)
			{
				Bounds = FBox2D(Min, Max);
				bHasPoint = true;
			}
			else
			{
				Bounds += Min;
				Bounds += Max;
			}
		}

		if (!bHasPoint)
		{
			return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
		}

		Bounds.Min -= FVector2D(Padding, Padding);
		Bounds.Max += FVector2D(Padding, Padding);
		return Bounds;
	}

	TArray<TSharedPtr<FJsonValue>> BuildImplementedInterfacesArray(const UBlueprint* Blueprint)
	{
		TArray<TSharedPtr<FJsonValue>> InterfaceArray;
		if (!Blueprint)
		{
			return InterfaceArray;
		}

		for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
		{
			TSharedPtr<FJsonObject> InterfaceObject = MakeShareable(new FJsonObject);
			const UClass* InterfaceClass = InterfaceDescription.Interface.Get();
			InterfaceObject->SetStringField(TEXT("interface_path"), InterfaceClass ? InterfaceClass->GetPathName() : TEXT(""));

			TArray<TSharedPtr<FJsonValue>> GraphsArray;
			for (const TObjectPtr<UEdGraph>& Graph : InterfaceDescription.Graphs)
			{
				if (Graph)
				{
					GraphsArray.Add(MakeShareable(new FJsonValueString(Graph->GetName())));
				}
			}

			InterfaceObject->SetArrayField(TEXT("graphs"), GraphsArray);
			InterfaceArray.Add(MakeShareable(new FJsonValueObject(InterfaceObject)));
		}

		return InterfaceArray;
	}
}
