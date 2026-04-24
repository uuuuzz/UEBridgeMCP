// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Utils/McpTypeDescriptorUtils.h"

#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"

namespace
{
	TSharedPtr<FMcpSchemaProperty> MakeTypeDescriptorSchemaRecursive(const FString& Description, int32 RemainingMapDepth)
	{
		TSharedPtr<FMcpSchemaProperty> TypeSchema = MakeShared<FMcpSchemaProperty>();
		TypeSchema->Type = TEXT("object");
		TypeSchema->Description = Description;
		TypeSchema->NestedRequired = { TEXT("kind") };
		TypeSchema->Properties.Add(TEXT("kind"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
			TEXT("Value kind"),
			{ TEXT("bool"), TEXT("byte"), TEXT("int"), TEXT("int64"), TEXT("float"), TEXT("double"), TEXT("name"), TEXT("string"), TEXT("text"), TEXT("struct"), TEXT("object"), TEXT("class"), TEXT("soft_object"), TEXT("soft_class"), TEXT("enum") },
			true)));
		TypeSchema->Properties.Add(TEXT("container"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
			TEXT("Container kind"),
			{ TEXT("single"), TEXT("array"), TEXT("set"), TEXT("map") })));
		TypeSchema->Properties.Add(TEXT("struct_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Struct path for struct kinds, e.g. '/Script/CoreUObject.Vector'"))));
		TypeSchema->Properties.Add(TEXT("object_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Class path for object and soft_object kinds"))));
		TypeSchema->Properties.Add(TEXT("class_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Class path for class and soft_class kinds"))));
		TypeSchema->Properties.Add(TEXT("enum_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Enum path for enum kinds"))));

		if (RemainingMapDepth > 0)
		{
			TypeSchema->Properties.Add(TEXT("map_key_type"), MakeTypeDescriptorSchemaRecursive(
				TEXT("Nested type descriptor for map keys"),
				RemainingMapDepth - 1));
			TypeSchema->Properties.Add(TEXT("map_value_type"), MakeTypeDescriptorSchemaRecursive(
				TEXT("Nested type descriptor for map values"),
				RemainingMapDepth - 1));
		}

		return TypeSchema;
	}

	template<typename TObjectType>
	TObjectType* LoadTypeObject(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty())
		{
			return nullptr;
		}

		if (TObjectType* Existing = FindObject<TObjectType>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		return LoadObject<TObjectType>(nullptr, *ObjectPath);
	}
}

TSharedPtr<FMcpSchemaProperty> McpTypeDescriptorUtils::MakeTypeDescriptorSchema(
	const FString& Description,
	int32 MaxMapNestingDepth)
{
	return MakeTypeDescriptorSchemaRecursive(Description, FMath::Max(0, MaxMapNestingDepth));
}

bool McpTypeDescriptorUtils::ParseTypeDescriptor(
	const TSharedPtr<FJsonObject>& TypeObject,
	FEdGraphPinType& OutPinType,
	FString& OutError)
{
	if (!TypeObject.IsValid())
	{
		OutError = TEXT("Type descriptor is null");
		return false;
	}

	OutPinType.ResetToDefaults();

	FString Container = TEXT("single");
	TypeObject->TryGetStringField(TEXT("container"), Container);

	FString Kind;
	const bool bHasKind = TypeObject->TryGetStringField(TEXT("kind"), Kind);
	const bool bIsMap = Container.Equals(TEXT("map"), ESearchCase::IgnoreCase);
	if (!bHasKind && !bIsMap)
	{
		OutError = TEXT("Type descriptor missing 'kind' field");
		return false;
	}

	if (bHasKind)
	{
		if (Kind == TEXT("bool"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (Kind == TEXT("byte"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		}
		else if (Kind == TEXT("int"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (Kind == TEXT("int64"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		}
		else if (Kind == TEXT("float") || Kind == TEXT("double"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = (Kind == TEXT("float")) ? UEdGraphSchema_K2::PC_Float : UEdGraphSchema_K2::PC_Double;
		}
		else if (Kind == TEXT("name"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (Kind == TEXT("string"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (Kind == TEXT("text"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		}
		else if (Kind == TEXT("struct"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			FString StructPath;
			if (TypeObject->TryGetStringField(TEXT("struct_path"), StructPath))
			{
				UScriptStruct* Struct = LoadTypeObject<UScriptStruct>(StructPath);
				if (!Struct)
				{
					OutError = FString::Printf(TEXT("Struct not found: '%s'"), *StructPath);
					return false;
				}
				OutPinType.PinSubCategoryObject = Struct;
			}
		}
		else if (Kind == TEXT("object") || Kind == TEXT("soft_object"))
		{
			OutPinType.PinCategory = (Kind == TEXT("object")) ? UEdGraphSchema_K2::PC_Object : UEdGraphSchema_K2::PC_SoftObject;
			FString ObjectClassPath;
			if (TypeObject->TryGetStringField(TEXT("object_class"), ObjectClassPath))
			{
				UClass* ObjectClass = LoadTypeObject<UClass>(ObjectClassPath);
				if (!ObjectClass)
				{
					OutError = FString::Printf(TEXT("Object class not found: '%s'"), *ObjectClassPath);
					return false;
				}
				OutPinType.PinSubCategoryObject = ObjectClass;
			}
		}
		else if (Kind == TEXT("class") || Kind == TEXT("soft_class"))
		{
			OutPinType.PinCategory = (Kind == TEXT("class")) ? UEdGraphSchema_K2::PC_Class : UEdGraphSchema_K2::PC_SoftClass;
			FString ClassPath;
			if (TypeObject->TryGetStringField(TEXT("class_path"), ClassPath))
			{
				UClass* ResolvedClass = LoadTypeObject<UClass>(ClassPath);
				if (!ResolvedClass)
				{
					OutError = FString::Printf(TEXT("Class not found: '%s'"), *ClassPath);
					return false;
				}
				OutPinType.PinSubCategoryObject = ResolvedClass;
			}
		}
		else if (Kind == TEXT("enum"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			FString EnumPath;
			if (TypeObject->TryGetStringField(TEXT("enum_path"), EnumPath))
			{
				UEnum* Enum = LoadTypeObject<UEnum>(EnumPath);
				if (!Enum)
				{
					OutError = FString::Printf(TEXT("Enum not found: '%s'"), *EnumPath);
					return false;
				}
				OutPinType.PinSubCategoryObject = Enum;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown type kind: '%s'"), *Kind);
			return false;
		}
	}

	if (Container.Equals(TEXT("single"), ESearchCase::IgnoreCase))
	{
		OutPinType.ContainerType = EPinContainerType::None;
	}
	else if (Container.Equals(TEXT("array"), ESearchCase::IgnoreCase))
	{
		OutPinType.ContainerType = EPinContainerType::Array;
	}
	else if (Container.Equals(TEXT("set"), ESearchCase::IgnoreCase))
	{
		OutPinType.ContainerType = EPinContainerType::Set;
	}
	else if (bIsMap)
	{
		const TSharedPtr<FJsonObject>* MapKeyTypeObject = nullptr;
		const bool bHasMapKeyType = TypeObject->TryGetObjectField(TEXT("map_key_type"), MapKeyTypeObject)
			&& MapKeyTypeObject && (*MapKeyTypeObject).IsValid();
		if (!bHasKind && !bHasMapKeyType)
		{
			OutError = TEXT("Map type descriptor requires 'map_key_type' or top-level 'kind'");
			return false;
		}

		FEdGraphPinType KeyType = OutPinType;
		if (bHasMapKeyType)
		{
			if (!ParseTypeDescriptor(*MapKeyTypeObject, KeyType, OutError))
			{
				return false;
			}
		}

		if (KeyType.IsContainer())
		{
			OutError = TEXT("Map key type cannot be a container");
			return false;
		}

		const TSharedPtr<FJsonObject>* MapValueTypeObject = nullptr;
		if (!TypeObject->TryGetObjectField(TEXT("map_value_type"), MapValueTypeObject)
			|| !MapValueTypeObject || !(*MapValueTypeObject).IsValid())
		{
			OutError = TEXT("Map type descriptor requires 'map_value_type'");
			return false;
		}

		FEdGraphPinType ValueType;
		if (!ParseTypeDescriptor(*MapValueTypeObject, ValueType, OutError))
		{
			return false;
		}

		if (ValueType.IsContainer())
		{
			OutError = TEXT("Map value type cannot be a container");
			return false;
		}

		OutPinType.PinCategory = KeyType.PinCategory;
		OutPinType.PinSubCategory = KeyType.PinSubCategory;
		OutPinType.PinSubCategoryObject = KeyType.PinSubCategoryObject;
		OutPinType.PinSubCategoryMemberReference = KeyType.PinSubCategoryMemberReference;
		OutPinType.bIsReference = KeyType.bIsReference;
		OutPinType.bIsConst = KeyType.bIsConst;
		OutPinType.bIsWeakPointer = KeyType.bIsWeakPointer;
		OutPinType.bIsUObjectWrapper = KeyType.bIsUObjectWrapper;
		OutPinType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
		OutPinType.ContainerType = EPinContainerType::Map;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown container kind: '%s'"), *Container);
		return false;
	}

	return true;
}
