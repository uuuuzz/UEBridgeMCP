// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/EditBlueprintMembersTool.h"
#include "Utils/McpAssetModifier.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UEditBlueprintMembersTool::GetToolDescription() const
{
	return TEXT("Edit Blueprint members: create/rename/delete variables, functions, and event dispatchers. "
		"Accepts a batched 'actions' array. Supports compile, save, dry_run, and rollback_on_error options.");
}

TMap<FString, FMcpSchemaProperty> UEditBlueprintMembersTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Blueprint asset path, e.g. '/Game/Blueprints/BP_Player'"), true));

	TSharedPtr<FMcpSchemaProperty> TypeSchema = MakeShared<FMcpSchemaProperty>();
	TypeSchema->Type = TEXT("object");
	TypeSchema->Description = TEXT("Type descriptor used for variables and function pins");
	TypeSchema->NestedRequired = {TEXT("kind")};
	TypeSchema->Properties.Add(TEXT("kind"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Value kind"),
		{TEXT("bool"), TEXT("byte"), TEXT("int"), TEXT("int64"), TEXT("float"), TEXT("double"), TEXT("name"), TEXT("string"), TEXT("text"), TEXT("struct"), TEXT("object"), TEXT("class"), TEXT("soft_object"), TEXT("soft_class"), TEXT("enum")},
		true)));
	TypeSchema->Properties.Add(TEXT("container"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Container kind"),
		{TEXT("single"), TEXT("array"), TEXT("set"), TEXT("map")})));
	TypeSchema->Properties.Add(TEXT("struct_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Struct path for struct kinds, e.g. '/Script/CoreUObject.Vector'"))));
	TypeSchema->Properties.Add(TEXT("object_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Class path for object kinds"))));
	TypeSchema->Properties.Add(TEXT("class_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Class path for class kinds"))));
	TypeSchema->Properties.Add(TEXT("enum_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Enum path for enum kinds"))));
	// 为 map_key_type / map_value_type 创建独立副本，避免循环引用导致 ToJson() 栈溢出
	auto MakeMapSubTypeSchema = [&]() -> TSharedPtr<FMcpSchemaProperty>
	{
		TSharedPtr<FMcpSchemaProperty> Sub = MakeShared<FMcpSchemaProperty>();
		Sub->Type = TypeSchema->Type;
		Sub->Description = TEXT("Nested type descriptor for map key/value (one level deep)");
		Sub->NestedRequired = TypeSchema->NestedRequired;
		// 复制除 map_key_type / map_value_type 之外的所有已注册子属性
		for (const auto& Pair : TypeSchema->Properties)
		{
			Sub->Properties.Add(Pair.Key, Pair.Value);
		}
		return Sub;
	};
	TypeSchema->Properties.Add(TEXT("map_key_type"), MakeMapSubTypeSchema());
	TypeSchema->Properties.Add(TEXT("map_value_type"), MakeMapSubTypeSchema());

	TSharedPtr<FMcpSchemaProperty> PinSchema = MakeShared<FMcpSchemaProperty>();
	PinSchema->Type = TEXT("object");
	PinSchema->Description = TEXT("Function pin descriptor");
	PinSchema->NestedRequired = {TEXT("name")};
	PinSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Pin name"), true)));
	PinSchema->Properties.Add(TEXT("type"), TypeSchema);
	PinSchema->Properties.Add(TEXT("default_value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("any"), TEXT("Default value for the pin"))));
	PinSchema->Properties.Add(TEXT("pass_by_reference"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the pin is passed by reference"))));
	PinSchema->Properties.Add(TEXT("is_const"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the pin is const"))));
	PinSchema->Properties.Add(TEXT("description"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional pin description"))));

	TSharedPtr<FJsonObject> MetadataRawSchema = MakeShareable(new FJsonObject);
	MetadataRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	MetadataRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> MetadataSchema = MakeShared<FMcpSchemaProperty>();
	MetadataSchema->Description = TEXT("Optional metadata map applied to the member definition");
	MetadataSchema->RawSchema = MetadataRawSchema;

	FMcpSchemaProperty PinArraySchema;
	PinArraySchema.Type = TEXT("array");
	PinArraySchema.Description = TEXT("Function pin descriptors");
	PinArraySchema.Items = PinSchema;

	TSharedPtr<FMcpSchemaProperty> ActionItemSchema = MakeShared<FMcpSchemaProperty>();
	ActionItemSchema->Type = TEXT("object");
	ActionItemSchema->Description = TEXT("Blueprint member edit action");
	ActionItemSchema->NestedRequired = {TEXT("action")};
	ActionItemSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Member edit action"),
		{TEXT("create_variable"), TEXT("rename_variable"), TEXT("delete_variable"), TEXT("set_variable_properties"), TEXT("create_function"), TEXT("rename_function"), TEXT("delete_function"), TEXT("set_function_signature"), TEXT("create_event_dispatcher")},
		true)));
	ActionItemSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing or new member name"))));
	ActionItemSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("New member name for rename actions"))));
	ActionItemSchema->Properties.Add(TEXT("type"), TypeSchema);
	ActionItemSchema->Properties.Add(TEXT("default_value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("any"), TEXT("Default value for create_variable"))));
	ActionItemSchema->Properties.Add(TEXT("category"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Member category"))));
	ActionItemSchema->Properties.Add(TEXT("tooltip"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Tooltip text"))));
	ActionItemSchema->Properties.Add(TEXT("instance_editable"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Expose variable to instance editing"))));
	ActionItemSchema->Properties.Add(TEXT("blueprint_read_only"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Mark variable as Blueprint read only"))));
	ActionItemSchema->Properties.Add(TEXT("expose_on_spawn"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Expose variable on spawn"))));
	ActionItemSchema->Properties.Add(TEXT("private"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Mark variable as private"))));
	ActionItemSchema->Properties.Add(TEXT("replication"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Replication mode for variables"),
		{TEXT("none"), TEXT("replicated"), TEXT("repnotify")})));
	ActionItemSchema->Properties.Add(TEXT("replicated_using"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("RepNotify function name when replication is repnotify"))));
	ActionItemSchema->Properties.Add(TEXT("pure"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the function is pure"))));
	ActionItemSchema->Properties.Add(TEXT("const"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the function is const"))));
	ActionItemSchema->Properties.Add(TEXT("call_in_editor"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether the function is callable in editor"))));
	ActionItemSchema->Properties.Add(TEXT("inputs"), MakeShared<FMcpSchemaProperty>(PinArraySchema));
	ActionItemSchema->Properties.Add(TEXT("outputs"), MakeShared<FMcpSchemaProperty>(PinArraySchema));
	ActionItemSchema->Properties.Add(TEXT("metadata"), MetadataSchema);

	FMcpSchemaProperty ActionsSchema;
	ActionsSchema.Type = TEXT("array");
	ActionsSchema.Description = TEXT("Array of Blueprint member edit actions. Uses nested descriptors for types, inputs, outputs, and metadata.");
	ActionsSchema.bRequired = true;
	ActionsSchema.Items = ActionItemSchema;
	Schema.Add(TEXT("actions"), ActionsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy after edits: 'never', 'if_needed', 'always'"),
		{TEXT("never"), TEXT("if_needed"), TEXT("always")}));

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save asset after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only, do not apply changes")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Roll back entire batch on first failure (default true)")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional FScopedTransaction label override")));
	Schema.Add(TEXT("max_diagnostics"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Max compile diagnostics to return (default 100)")));

	return Schema;
}

TArray<FString> UEditBlueprintMembersTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("actions")};
}

namespace
{
	constexpr const TCHAR* VariableToolTipMetaDataKey = TEXT("Tooltip");

	FString InferBlueprintMembersErrorCode(const FString& ActionName, const FString& ErrorMessage)
	{
		if (ErrorMessage.Contains(TEXT("already exists"), ESearchCase::IgnoreCase))
		{
			if (ActionName == TEXT("create_variable") || ActionName == TEXT("rename_variable"))
			{
				return TEXT("UEBMCP_BLUEPRINT_VARIABLE_ALREADY_EXISTS");
			}
			if (ActionName == TEXT("create_function") || ActionName == TEXT("rename_function"))
			{
				return TEXT("UEBMCP_BLUEPRINT_FUNCTION_ALREADY_EXISTS");
			}
			if (ActionName == TEXT("create_event_dispatcher"))
			{
				return TEXT("UEBMCP_BLUEPRINT_DISPATCHER_ALREADY_EXISTS");
			}
		}

		if (ErrorMessage.Contains(TEXT("not found"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_BLUEPRINT_MEMBER_NOT_FOUND");
		}

		if (ActionName == TEXT("set_function_signature")
			|| ErrorMessage.Contains(TEXT("pin"), ESearchCase::IgnoreCase)
			|| ErrorMessage.Contains(TEXT("signature"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_BLUEPRINT_SIGNATURE_INVALID");
		}

		if (ErrorMessage.Contains(TEXT("Missing 'action' field"), ESearchCase::IgnoreCase)
			|| ErrorMessage.Contains(TEXT("is required"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_MISSING_REQUIRED_FIELD");
		}

		if (ErrorMessage.Contains(TEXT("Unsupported action"), ESearchCase::IgnoreCase))
		{
			return TEXT("UEBMCP_UNSUPPORTED_ACTION");
		}

		return TEXT("UEBMCP_INVALID_ACTION");
	}

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
}

bool UEditBlueprintMembersTool::GetFunctionNodes(
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

bool UEditBlueprintMembersTool::ConvertMetadataValueToString(
	const TSharedPtr<FJsonValue>& JsonValue,
	FString& OutString)
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

void UEditBlueprintMembersTool::SetOrClearBlueprintVariableMetaData(
	UBlueprint* Blueprint,
	const FName VarName,
	const FName MetaDataKey,
	bool bShouldSet,
	const FString& EnabledValue)
{
	if (bShouldSet)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarName, nullptr, MetaDataKey, EnabledValue);
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VarName, nullptr, MetaDataKey);
	}
}

void UEditBlueprintMembersTool::SetOrClearBlueprintVariableMetaData(
	UBlueprint* Blueprint,
	const FName VarName,
	const FName MetaDataKey,
	const FString& Value)
{
	if (Value.IsEmpty())
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VarName, nullptr, MetaDataKey);
	}
	else
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarName, nullptr, MetaDataKey, Value);
	}
}

void UEditBlueprintMembersTool::ApplyVariableMetadataObject(
	UBlueprint* Blueprint,
	const FName VarName,
	const TSharedPtr<FJsonObject>& MetadataObject)
{
	if (!Blueprint || !MetadataObject.IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : MetadataObject->Values)
	{
		FString MetadataValue;
		if (ConvertMetadataValueToString(Pair.Value, MetadataValue))
		{
			SetOrClearBlueprintVariableMetaData(Blueprint, VarName, FName(*Pair.Key), MetadataValue);
		}
	}
}

bool UEditBlueprintMembersTool::ApplyFunctionMetadataObject(
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

bool UEditBlueprintMembersTool::ApplyVariableActionSettings(
	UBlueprint* Blueprint,
	const FName VarName,
	const TSharedPtr<FJsonObject>& Action,
	bool bAllowTypeChange,
	FString& OutError)
{
	if (!Blueprint || !Action.IsValid())
	{
		OutError = TEXT("Invalid Blueprint or action");
		return false;
	}

	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName);
	if (VarIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found"), *VarName.ToString());
		return false;
	}

	if (bAllowTypeChange)
	{
		const TSharedPtr<FJsonObject>* TypeObject = nullptr;
		if (Action->TryGetObjectField(TEXT("type"), TypeObject) && TypeObject && (*TypeObject).IsValid())
		{
			FEdGraphPinType NewPinType;
			if (!ParseTypeDescriptor(*TypeObject, NewPinType, OutError))
			{
				return false;
			}

			if (Blueprint->NewVariables[VarIndex].VarType != NewPinType)
			{
				FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, VarName, NewPinType);
				VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName);
				if (VarIndex == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Variable '%s' not found after type update"), *VarName.ToString());
					return false;
				}
			}
		}
	}

	FBPVariableDescription& VariableDescription = Blueprint->NewVariables[VarIndex];

	const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
	if (Action->TryGetObjectField(TEXT("metadata"), MetadataObject) && MetadataObject && (*MetadataObject).IsValid())
	{
		ApplyVariableMetadataObject(Blueprint, VarName, *MetadataObject);
	}

	if (Action->HasField(TEXT("default_value")))
	{
		VariableDescription.DefaultValue = JsonValueToDefaultString(Action->TryGetField(TEXT("default_value")), VariableDescription.VarType);
	}

	FString Category;
	if (Action->TryGetStringField(TEXT("category"), Category))
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, VarName, nullptr, FText::FromString(Category));
	}

	FString Tooltip;
	if (Action->TryGetStringField(TEXT("tooltip"), Tooltip))
	{
		SetOrClearBlueprintVariableMetaData(Blueprint, VarName, FName(VariableToolTipMetaDataKey), Tooltip);
	}

	bool bInstanceEditable = false;
	if (Action->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable))
	{
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VarName, !bInstanceEditable);
	}

	bool bBlueprintReadOnly = false;
	if (Action->TryGetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly))
	{
		FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VarName, bBlueprintReadOnly);
	}

	bool bExposeOnSpawn = false;
	if (Action->TryGetBoolField(TEXT("expose_on_spawn"), bExposeOnSpawn))
	{
		SetOrClearBlueprintVariableMetaData(Blueprint, VarName, FBlueprintMetadata::MD_ExposeOnSpawn, bExposeOnSpawn);
	}

	bool bPrivate = false;
	if (Action->TryGetBoolField(TEXT("private"), bPrivate))
	{
		SetOrClearBlueprintVariableMetaData(Blueprint, VarName, FBlueprintMetadata::MD_Private, bPrivate);
	}

	FString Replication;
	if (Action->TryGetStringField(TEXT("replication"), Replication))
	{
		VariableDescription.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
		VariableDescription.RepNotifyFunc = NAME_None;

		if (Replication.Equals(TEXT("replicated"), ESearchCase::IgnoreCase))
		{
			VariableDescription.PropertyFlags |= CPF_Net;
		}
		else if (Replication.Equals(TEXT("repnotify"), ESearchCase::IgnoreCase))
		{
			FString RepNotifyFunctionName;
			if (!Action->TryGetStringField(TEXT("replicated_using"), RepNotifyFunctionName) || RepNotifyFunctionName.IsEmpty())
			{
				RepNotifyFunctionName = FString::Printf(TEXT("OnRep_%s"), *VarName.ToString());
			}

			VariableDescription.PropertyFlags |= (CPF_Net | CPF_RepNotify);
			VariableDescription.RepNotifyFunc = FName(*RepNotifyFunctionName);
		}
		else if (!Replication.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("Unsupported replication mode '%s'"), *Replication);
			return false;
		}
	}

	return true;
}

bool UEditBlueprintMembersTool::ApplyFunctionActionSettings(
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& Action,
	bool bAllowOutputs,
	FString& OutError)
{
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	if (!GetFunctionNodes(Graph, EntryNode, ResultNode, OutError))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* InputDescriptors = nullptr;
	if (Action->TryGetArrayField(TEXT("inputs"), InputDescriptors))
	{
		if (!SynchronizeUserDefinedPins(EntryNode, InputDescriptors, EGPD_Output, OutError))
		{
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* OutputDescriptors = nullptr;
	if (Action->TryGetArrayField(TEXT("outputs"), OutputDescriptors))
	{
		if (!bAllowOutputs)
		{
			OutError = TEXT("This member does not support output pins");
			return false;
		}

		if (!ResultNode)
		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			K2Schema->CreateFunctionGraphTerminators(*Graph, (UClass*)nullptr);
			if (!GetFunctionNodes(Graph, EntryNode, ResultNode, OutError) || !ResultNode)
			{
				OutError = FString::Printf(TEXT("Function result node not found in '%s'"), *Graph->GetName());
				return false;
			}
		}

		if (!SynchronizeUserDefinedPins(ResultNode, OutputDescriptors, EGPD_Input, OutError))
		{
			return false;
		}
	}

	FKismetUserDeclaredFunctionMetadata* FunctionMetaData = FBlueprintEditorUtils::GetGraphFunctionMetaData(Graph);
	if (!FunctionMetaData)
	{
		OutError = FString::Printf(TEXT("Function metadata is unavailable for graph '%s'"), *Graph->GetName());
		return false;
	}

	const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
	if (Action->TryGetObjectField(TEXT("metadata"), MetadataObject) && MetadataObject && (*MetadataObject).IsValid())
	{
		if (!ApplyFunctionMetadataObject(*FunctionMetaData, *MetadataObject, OutError))
		{
			return false;
		}
	}

	FString Category;
	if (Action->TryGetStringField(TEXT("category"), Category))
	{
		FunctionMetaData->Category = FText::FromString(Category);
	}

	FString Tooltip;
	if (Action->TryGetStringField(TEXT("tooltip"), Tooltip))
	{
		FunctionMetaData->ToolTip = FText::FromString(Tooltip);
	}

	bool bCallInEditor = false;
	if (Action->TryGetBoolField(TEXT("call_in_editor"), bCallInEditor))
	{
		FunctionMetaData->bCallInEditor = bCallInEditor;
	}

	int32 ExtraFlags = EntryNode->GetExtraFlags();
	bool bShouldUpdateExtraFlags = false;

	bool bPure = false;
	if (Action->TryGetBoolField(TEXT("pure"), bPure))
	{
		if (bPure)
		{
			ExtraFlags |= FUNC_BlueprintPure;
		}
		else
		{
			ExtraFlags &= ~FUNC_BlueprintPure;
		}
		bShouldUpdateExtraFlags = true;
	}

	bool bConstFunction = false;
	if (Action->TryGetBoolField(TEXT("const"), bConstFunction))
	{
		if (bConstFunction)
		{
			ExtraFlags |= FUNC_Const;
		}
		else
		{
			ExtraFlags &= ~FUNC_Const;
		}
		bShouldUpdateExtraFlags = true;
	}

	if (bShouldUpdateExtraFlags)
	{
		EntryNode->SetExtraFlags(ExtraFlags);
	}

	return true;
}

bool UEditBlueprintMembersTool::SynchronizeUserDefinedPins(
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

FMcpToolResult UEditBlueprintMembersTool::Execute(

	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	// Parse arguments.
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actions"), ActionsArray) || !ActionsArray || ActionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actions' array is required and must not be empty"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("never"));
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Blueprint Members"));
	const int32 MaxDiagnostics = GetIntArgOrDefault(Arguments, TEXT("max_diagnostics"), 100);

	// Load the Blueprint asset.
	FString LoadError;
	UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	// Start a transaction for batched edits.
	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
	}

	// Execute actions.
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	TArray<FString> Warnings;
	bool bAnyFailed = false;
	bool bStructuralChange = false;

	for (int32 i = 0; i < ActionsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& ActionValue = (*ActionsArray)[i];
		const TSharedPtr<FJsonObject>* ActionObj = nullptr;
		if (!ActionValue.IsValid() || !ActionValue->TryGetObject(ActionObj) || !ActionObj || !(*ActionObj).IsValid())
		{
			if (bRollbackOnError)
			{
				Transaction.Reset(); // Roll back the transaction.
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Action at index %d is not a valid object"), i));
			}
			continue;
		}

		TSharedPtr<FJsonObject> ActionResult = MakeShareable(new FJsonObject);
		FString ActionError;

		bool bSuccess = false;
		if (!bDryRun)
		{
			bSuccess = ExecuteAction(Blueprint, *ActionObj, i, ActionResult, ActionError);
		}
		else
		{
			// In dry-run mode, validate only the action field.
			FString ActionName;
			if ((*ActionObj)->TryGetStringField(TEXT("action"), ActionName))
			{
				ActionResult->SetStringField(TEXT("action"), ActionName);
				bSuccess = true;
			}
			else
			{
				ActionError = TEXT("Missing 'action' field");
			}
		}

		ActionResult->SetNumberField(TEXT("index"), i);
		ActionResult->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess)
		{
			const FString ErrorCode = InferBlueprintMembersErrorCode(ActionResult->GetStringField(TEXT("action")), ActionError);
			ActionResult->SetStringField(TEXT("error"), ActionError);
			ActionResult->SetStringField(TEXT("code"), ErrorCode);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));

			if (bRollbackOnError && !bDryRun)
			{
				Transaction.Reset(); // Roll back the transaction.
				TSharedPtr<FJsonObject> ErrorDetails = MakeShareable(new FJsonObject);
				ErrorDetails->SetStringField(TEXT("asset_path"), AssetPath);
				ErrorDetails->SetNumberField(TEXT("failed_action_index"), i);
				ErrorDetails->SetStringField(TEXT("action"), ActionResult->GetStringField(TEXT("action")));

				TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
				PartialResult->SetArrayField(TEXT("results"), ResultsArray);
				PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
				return FMcpToolResult::StructuredError(ErrorCode, ActionError, ErrorDetails, PartialResult);
			}
		}
		else
		{
			bStructuralChange = true;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActionResult)));
	}

	// Compile if requested.
	TSharedPtr<FJsonObject> CompileResult = MakeShareable(new FJsonObject);
	bool bCompileAttempted = false;
	bool bCompileSuccess = true;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;

	if (!bDryRun && bStructuralChange)
	{
		bool bShouldCompile = (CompilePolicy == TEXT("always")) ||
			(CompilePolicy == TEXT("if_needed") && bStructuralChange);

		if (bShouldCompile)
		{
			bCompileAttempted = true;
			FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);

			FString CompileError;
			bCompileSuccess = FMcpAssetModifier::CompileBlueprint(Blueprint, CompileError);
			if (!bCompileSuccess)
			{
				TSharedPtr<FJsonObject> DiagItem = MakeShareable(new FJsonObject);
				DiagItem->SetStringField(TEXT("severity"), TEXT("error"));
				DiagItem->SetStringField(TEXT("code"), TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"));
				DiagItem->SetStringField(TEXT("message"), CompileError);
				DiagItem->SetStringField(TEXT("asset_path"), AssetPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(DiagItem)));

				TSharedPtr<FJsonObject> PartialResultObject = MakeShareable(new FJsonObject);
				PartialResultObject->SetStringField(TEXT("stage"), TEXT("compile"));
				PartialResultObject->SetBoolField(TEXT("success"), false);
				PartialResultObject->SetStringField(TEXT("error"), CompileError);
				PartialResultObject->SetStringField(TEXT("asset_path"), AssetPath);
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(PartialResultObject)));
				bAnyFailed = true;
			}
		}
	}

	CompileResult->SetBoolField(TEXT("attempted"), bCompileAttempted);
	CompileResult->SetBoolField(TEXT("success"), bCompileSuccess);
	CompileResult->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);

	// 保存
	if (!bDryRun && bSave && !bAnyFailed)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
		{
			Warnings.Add(FString::Printf(TEXT("Save failed: %s"), *SaveError));
		}
	}

	// 构建响应
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("edit-blueprint-members"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	ModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);

	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetObjectField(TEXT("compile"), CompileResult);

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	for (const FString& W : Warnings)
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(W)));
	}
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

bool UEditBlueprintMembersTool::ExecuteAction(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	int32 Index,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString ActionName;
	if (!Action->TryGetStringField(TEXT("action"), ActionName))
	{
		OutError = TEXT("Missing 'action' field");
		return false;
	}

	OutResult->SetStringField(TEXT("action"), ActionName);

	if (ActionName == TEXT("create_variable"))
	{
		return CreateVariable(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("rename_variable"))
	{
		return RenameVariable(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("delete_variable"))
	{
		return DeleteVariable(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("set_variable_properties"))
	{
		return SetVariableProperties(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("create_function"))
	{
		return CreateFunction(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("rename_function"))
	{
		return RenameFunction(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("delete_function"))
	{
		return DeleteFunction(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("set_function_signature"))
	{
		return SetFunctionSignature(Blueprint, Action, OutResult, OutError);
	}
	else if (ActionName == TEXT("create_event_dispatcher"))
	{
		return CreateEventDispatcher(Blueprint, Action, OutResult, OutError);
	}

	OutError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
	return false;
}

// ========== 类型解析辅助 ==========

bool UEditBlueprintMembersTool::ParseTypeDescriptor(
	const TSharedPtr<FJsonObject>& TypeObj,
	FEdGraphPinType& OutPinType,
	FString& OutError)
{
	if (!TypeObj.IsValid())
	{
		OutError = TEXT("Type descriptor is null");
		return false;
	}

	OutPinType.ResetToDefaults();

	FString Container = TEXT("single");
	TypeObj->TryGetStringField(TEXT("container"), Container);

	FString Kind;
	const bool bHasKind = TypeObj->TryGetStringField(TEXT("kind"), Kind);
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
			OutPinType.PinSubCategory = (Kind == TEXT("float"))
				? UEdGraphSchema_K2::PC_Float
				: UEdGraphSchema_K2::PC_Double;
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
			if (TypeObj->TryGetStringField(TEXT("struct_path"), StructPath))
			{
				UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
				if (!Struct)
				{
					Struct = LoadObject<UScriptStruct>(nullptr, *StructPath);
				}
				if (Struct)
				{
					OutPinType.PinSubCategoryObject = Struct;
				}
				else
				{
					OutError = FString::Printf(TEXT("Struct not found: '%s'"), *StructPath);
					return false;
				}
			}
		}
		else if (Kind == TEXT("object") || Kind == TEXT("soft_object"))
		{
			OutPinType.PinCategory = (Kind == TEXT("object"))
				? UEdGraphSchema_K2::PC_Object
				: UEdGraphSchema_K2::PC_SoftObject;
			FString ObjectClassPath;
			if (TypeObj->TryGetStringField(TEXT("object_class"), ObjectClassPath))
			{
				UClass* Class = FindObject<UClass>(nullptr, *ObjectClassPath);
				if (!Class)
				{
					Class = LoadObject<UClass>(nullptr, *ObjectClassPath);
				}
				if (Class)
				{
					OutPinType.PinSubCategoryObject = Class;
				}
				else
				{
					OutError = FString::Printf(TEXT("Object class not found: '%s'"), *ObjectClassPath);
					return false;
				}
			}
		}
		else if (Kind == TEXT("class") || Kind == TEXT("soft_class"))
		{
			OutPinType.PinCategory = (Kind == TEXT("class"))
				? UEdGraphSchema_K2::PC_Class
				: UEdGraphSchema_K2::PC_SoftClass;
			FString ClassPath;
			if (TypeObj->TryGetStringField(TEXT("class_path"), ClassPath))
			{
				UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
				if (!Class)
				{
					Class = LoadObject<UClass>(nullptr, *ClassPath);
				}
				if (Class)
				{
					OutPinType.PinSubCategoryObject = Class;
				}
				else
				{
					OutError = FString::Printf(TEXT("Class not found: '%s'"), *ClassPath);
					return false;
				}
			}
		}
		else if (Kind == TEXT("enum"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			FString EnumPath;
			if (TypeObj->TryGetStringField(TEXT("enum_path"), EnumPath))
			{
				UEnum* Enum = FindObject<UEnum>(nullptr, *EnumPath);
				if (!Enum)
				{
					Enum = LoadObject<UEnum>(nullptr, *EnumPath);
				}
				if (Enum)
				{
					OutPinType.PinSubCategoryObject = Enum;
				}
				else
				{
					OutError = FString::Printf(TEXT("Enum not found: '%s'"), *EnumPath);
					return false;
				}
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
		const bool bHasMapKeyType = TypeObj->TryGetObjectField(TEXT("map_key_type"), MapKeyTypeObject)
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
		if (!TypeObj->TryGetObjectField(TEXT("map_value_type"), MapValueTypeObject)
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

// ========== 变量操作 ==========

bool UEditBlueprintMembersTool::CreateVariable(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString VarName;
	if (!Action->TryGetStringField(TEXT("name"), VarName))
	{
		OutError = TEXT("'name' is required for create_variable");
		return false;
	}

	const TSharedPtr<FJsonObject>* TypeObject = nullptr;
	if (!Action->TryGetObjectField(TEXT("type"), TypeObject) || !TypeObject || !(*TypeObject).IsValid())
	{
		OutError = TEXT("'type' is required for create_variable");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), VarName);
	const FName VarFName(*VarName);
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarFName) != INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Variable '%s' already exists"), *VarName);
		return false;
	}

	FEdGraphPinType PinType;
	if (!ParseTypeDescriptor(*TypeObject, PinType, OutError))
	{
		return false;
	}

	const FString DefaultValueString = Action->HasField(TEXT("default_value"))
		? JsonValueToDefaultString(Action->TryGetField(TEXT("default_value")), PinType)
		: FString();

	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarFName, PinType, DefaultValueString))
	{
		OutError = FString::Printf(TEXT("Failed to add variable '%s'"), *VarName);
		return false;
	}

	if (!ApplyVariableActionSettings(Blueprint, VarFName, Action, false, OutError))
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarFName);
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintMembersTool::RenameVariable(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString OldName, NewName;
	if (!Action->TryGetStringField(TEXT("name"), OldName) || !Action->TryGetStringField(TEXT("new_name"), NewName))
	{
		OutError = TEXT("'name' and 'new_name' are required for rename_variable");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), OldName);
	OutResult->SetStringField(TEXT("new_name"), NewName);

	const FName OldVarName(*OldName);
	const FName NewVarName(*NewName);
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, OldVarName) == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found"), *OldName);
		return false;
	}

	if (OldVarName != NewVarName && FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, NewVarName) != INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Variable '%s' already exists"), *NewName);
		return false;
	}

	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, OldVarName, NewVarName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintMembersTool::DeleteVariable(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString VarName;
	if (!Action->TryGetStringField(TEXT("name"), VarName))
	{
		OutError = TEXT("'name' is required for delete_variable");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), VarName);

	const FName VariableFName(*VarName);
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableFName) == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found"), *VarName);
		return false;
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VariableFName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintMembersTool::SetVariableProperties(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString VarName;
	if (!Action->TryGetStringField(TEXT("name"), VarName))
	{
		OutError = TEXT("'name' is required for set_variable_properties");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), VarName);
	const FName VarFName(*VarName);
	if (!ApplyVariableActionSettings(Blueprint, VarFName, Action, true, OutError))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

// ========== 函数操作 ==========

bool UEditBlueprintMembersTool::CreateFunction(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString FunctionName;
	if (!Action->TryGetStringField(TEXT("name"), FunctionName))
	{
		OutError = TEXT("'name' is required for create_function");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), FunctionName);
	if (FMcpAssetModifier::FindGraphByName(Blueprint, FunctionName))
	{
		OutError = FString::Printf(TEXT("Function '%s' already exists"), *FunctionName);
		return false;
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	if (!NewGraph)
	{
		OutError = FString::Printf(TEXT("Failed to create function graph '%s'"), *FunctionName);
		return false;
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, true, nullptr);
	if (!ApplyFunctionActionSettings(NewGraph, Action, true, OutError))
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, NewGraph, EGraphRemoveFlags::MarkTransient);
		return false;
	}

	OutResult->SetStringField(TEXT("graph_name"), FunctionName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintMembersTool::RenameFunction(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString OldName, NewName;
	if (!Action->TryGetStringField(TEXT("name"), OldName) || !Action->TryGetStringField(TEXT("new_name"), NewName))
	{
		OutError = TEXT("'name' and 'new_name' are required for rename_function");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), OldName);
	OutResult->SetStringField(TEXT("new_name"), NewName);

	UEdGraph* Graph = FMcpAssetModifier::FindGraphByName(Blueprint, OldName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found"), *OldName);
		return false;
	}

	if (!OldName.Equals(NewName, ESearchCase::CaseSensitive) && FMcpAssetModifier::FindGraphByName(Blueprint, NewName))
	{
		OutError = FString::Printf(TEXT("Function '%s' already exists"), *NewName);
		return false;
	}

	FBlueprintEditorUtils::RenameGraph(Graph, NewName);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintMembersTool::DeleteFunction(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString FunctionName;
	if (!Action->TryGetStringField(TEXT("name"), FunctionName))
	{
		OutError = TEXT("'name' is required for delete_function");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), FunctionName);

	UEdGraph* Graph = FMcpAssetModifier::FindGraphByName(Blueprint, FunctionName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found"), *FunctionName);
		return false;
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditBlueprintMembersTool::SetFunctionSignature(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString FunctionName;
	if (!Action->TryGetStringField(TEXT("name"), FunctionName))
	{
		OutError = TEXT("'name' is required for set_function_signature");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), FunctionName);

	UEdGraph* Graph = FMcpAssetModifier::FindGraphByName(Blueprint, FunctionName);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found"), *FunctionName);
		return false;
	}

	if (!ApplyFunctionActionSettings(Graph, Action, true, OutError))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

// ========== 事件分发器操作 ==========

bool UEditBlueprintMembersTool::CreateEventDispatcher(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& Action,
	TSharedPtr<FJsonObject>& OutResult,
	FString& OutError)
{
	FString DispatcherName;
	if (!Action->TryGetStringField(TEXT("name"), DispatcherName))
	{
		OutError = TEXT("'name' is required for create_event_dispatcher");
		return false;
	}

	OutResult->SetStringField(TEXT("name"), DispatcherName);
	const FName DispatcherFName(*DispatcherName);

	for (const FBPVariableDescription& VariableDescription : Blueprint->NewVariables)
	{
		if (VariableDescription.VarName == DispatcherFName && VariableDescription.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			OutError = FString::Printf(TEXT("Event dispatcher '%s' already exists"), *DispatcherName);
			return false;
		}
	}

	if (FMcpAssetModifier::FindGraphByName(Blueprint, DispatcherName))
	{
		OutError = FString::Printf(TEXT("Event dispatcher '%s' already exists"), *DispatcherName);
		return false;
	}

	FEdGraphPinType DelegatePinType;
	DelegatePinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherFName, DelegatePinType))
	{
		OutError = FString::Printf(TEXT("Failed to add dispatcher variable '%s'"), *DispatcherName);
		return false;
	}

	UEdGraph* SignatureGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		DispatcherFName,
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	if (!SignatureGraph)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherFName);
		OutError = FString::Printf(TEXT("Failed to create dispatcher signature graph '%s'"), *DispatcherName);
		return false;
	}

	SignatureGraph->bEditable = false;
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->CreateDefaultNodesForGraph(*SignatureGraph);
	K2Schema->CreateFunctionGraphTerminators(*SignatureGraph, (UClass*)nullptr);
	K2Schema->AddExtraFunctionFlags(SignatureGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
	K2Schema->MarkFunctionEntryAsEditable(SignatureGraph, true);
	Blueprint->DelegateSignatureGraphs.Add(SignatureGraph);

	if (!ApplyFunctionActionSettings(SignatureGraph, Action, false, OutError))
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, SignatureGraph, EGraphRemoveFlags::MarkTransient);
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherFName);
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

// ========== Helper: convert a JSON value to a Blueprint variable default string ==========

FString UEditBlueprintMembersTool::JsonValueToDefaultString(
	const TSharedPtr<FJsonValue>& JsonValue,
	const FEdGraphPinType& PinType)
{
	if (!JsonValue.IsValid() || JsonValue->Type == EJson::Null)
	{
		return FString();
	}

	const FString Category = PinType.PinCategory.ToString();

	// Boolean values.
	if (Category == UEdGraphSchema_K2::PC_Boolean)
	{
		bool bVal = false;
		if (JsonValue->TryGetBool(bVal))
		{
			return bVal ? TEXT("true") : TEXT("false");
		}
		// Also accept the string forms "true" and "false".
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal.ToLower();
		}
		return TEXT("false");
	}

	// Numeric values (int, int64, byte, float, double).
	if (Category == UEdGraphSchema_K2::PC_Int
		|| Category == UEdGraphSchema_K2::PC_Int64
		|| Category == UEdGraphSchema_K2::PC_Byte
		|| Category == UEdGraphSchema_K2::PC_Real)
	{
		double NumVal = 0.0;
		if (JsonValue->TryGetNumber(NumVal))
		{
			// Integer categories should not include a decimal point.
			if (Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Byte)
			{
				return FString::Printf(TEXT("%d"), FMath::RoundToInt32(NumVal));
			}
			if (Category == UEdGraphSchema_K2::PC_Int64)
			{
				return FString::Printf(TEXT("%lld"), static_cast<int64>(NumVal));
			}
			// Float and double categories.
			return FString::SanitizeFloat(NumVal);
		}
		// Also accept numeric values encoded as strings.
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal;
		}
		return TEXT("0");
	}

	// String, Name, and Text categories.
	if (Category == UEdGraphSchema_K2::PC_String
		|| Category == UEdGraphSchema_K2::PC_Name
		|| Category == UEdGraphSchema_K2::PC_Text)
	{
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal;
		}
		// Serialize non-string JSON primitives as strings.
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

	// Struct categories such as Vector, Rotator, and LinearColor.
	// Unreal uses a parenthesized default-value syntax such as "(X=0.0,Y=0.0,Z=0.0)".
	if (Category == UEdGraphSchema_K2::PC_Struct)
	{
		// If the input is already a string, use it directly.
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal;
		}

		// If the input is an array, convert common struct layouts to Unreal syntax.
		const TArray<TSharedPtr<FJsonValue>>* ArrayVal = nullptr;
		if (JsonValue->TryGetArray(ArrayVal) && ArrayVal)
		{
			UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
			if (Struct)
			{
				FString StructName = Struct->GetName();
				if ((StructName == TEXT("Vector") || StructName == TEXT("Vector3d")) && ArrayVal->Num() >= 3)
				{
					return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
						*(*ArrayVal)[0]->AsString(), *(*ArrayVal)[1]->AsString(), *(*ArrayVal)[2]->AsString());
				}
				if ((StructName == TEXT("Rotator") || StructName == TEXT("Rotator3d")) && ArrayVal->Num() >= 3)
				{
					return FString::Printf(TEXT("(Pitch=%s,Yaw=%s,Roll=%s)"),
						*(*ArrayVal)[0]->AsString(), *(*ArrayVal)[1]->AsString(), *(*ArrayVal)[2]->AsString());
				}
				if (StructName == TEXT("LinearColor") && ArrayVal->Num() >= 3)
				{
					FString Alpha = (ArrayVal->Num() >= 4) ? (*ArrayVal)[3]->AsString() : TEXT("1.0");
					return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"),
						*(*ArrayVal)[0]->AsString(), *(*ArrayVal)[1]->AsString(), *(*ArrayVal)[2]->AsString(), *Alpha);
				}
				if (StructName == TEXT("Vector2D") && ArrayVal->Num() >= 2)
				{
					return FString::Printf(TEXT("(X=%s,Y=%s)"),
						*(*ArrayVal)[0]->AsString(), *(*ArrayVal)[1]->AsString());
				}
			}
		}

		// If the input is an object, build Unreal's parenthesized key-value syntax.
		const TSharedPtr<FJsonObject>* ObjVal = nullptr;
		if (JsonValue->TryGetObject(ObjVal) && ObjVal && (*ObjVal).IsValid())
		{
			FString Result = TEXT("(");
			bool bFirst = true;
			for (const auto& Pair : (*ObjVal)->Values)
			{
				if (!bFirst)
				{
					Result += TEXT(",");
				}
				Result += FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value->AsString());
				bFirst = false;
			}
			Result += TEXT(")");
			return Result;
		}

		return FString();
	}

	// Enum categories.
	if (Category == UEdGraphSchema_K2::PC_Byte && PinType.PinSubCategoryObject.IsValid())
	{
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal;
		}
		double NumVal = 0.0;
		if (JsonValue->TryGetNumber(NumVal))
		{
			return FString::Printf(TEXT("%d"), FMath::RoundToInt32(NumVal));
		}
		return FString();
	}

	// Object and class reference categories use asset-path strings.
	if (Category == UEdGraphSchema_K2::PC_Object
		|| Category == UEdGraphSchema_K2::PC_SoftObject
		|| Category == UEdGraphSchema_K2::PC_Class
		|| Category == UEdGraphSchema_K2::PC_SoftClass)
	{
		FString StrVal;
		if (JsonValue->TryGetString(StrVal))
		{
			return StrVal;
		}
		return FString();
	}

	// Fallback: return a string value if one is available.
	FString StrVal;
	if (JsonValue->TryGetString(StrVal))
	{
		return StrVal;
	}

	return FString();
}