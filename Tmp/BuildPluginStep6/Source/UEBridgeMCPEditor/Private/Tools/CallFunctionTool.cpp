// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/CallFunctionTool.h"
#include "Tools/PIE/PieSessionTool.h"
#include "Utils/McpAssetModifier.h"
#include "UEBridgeMCPEditor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"

FString UCallFunctionTool::GetToolDescription() const
{
	return TEXT("Call functions on actors, components, or global Blueprint libraries. "
		"Target format: 'ActorName.FunctionName', 'ActorName.ComponentName.FunctionName', or '/Game/BP_Lib.FunctionName'. "
		"Use 'world' param: 'editor' (default) or 'pie'.");
}

TMap<FString, FMcpSchemaProperty> UCallFunctionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty Target;
	Target.Type = TEXT("string");
	Target.Description = TEXT("Function target: 'ActorName.Function', 'ActorName.Component.Function', or '/Game/BP.Function'");
	Target.bRequired = true;
	Schema.Add(TEXT("target"), Target);

	FMcpSchemaProperty Arguments;
	Arguments.Type = TEXT("object");
	Arguments.Description = TEXT("Function arguments as {\"ParamName\": value}");
	Arguments.bRequired = false;
	Schema.Add(TEXT("arguments"), Arguments);

	FMcpSchemaProperty WorldParam;
	WorldParam.Type = TEXT("string");
	WorldParam.Description = TEXT("Target world: 'editor' (default) or 'pie'");
	WorldParam.bRequired = false;
	Schema.Add(TEXT("world"), WorldParam);

	return Schema;
}

FMcpToolResult UCallFunctionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	const FString Target = GetStringArgOrDefault(Arguments, TEXT("target"));
	const FString WorldParam = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));

	if (Target.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'target' is required"));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("call-function: Target='%s' World='%s'"), *Target, *WorldParam);

	if (Target.StartsWith(TEXT("/")))
	{
		int32 LastDotIndex = INDEX_NONE;
		if (!Target.FindLastChar('.', LastDotIndex))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("Invalid target format. Expected: /Game/BP_Lib.FunctionName"));
		}

		const FString BlueprintPath = Target.Left(LastDotIndex);
		const FString FunctionName = Target.Mid(LastDotIndex + 1);
		return CallGlobalFunction(BlueprintPath, FunctionName, Arguments);
	}

	if (WorldParam.Equals(TEXT("pie"), ESearchCase::IgnoreCase) && UPieSessionTool::IsPIETransitioning())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("PIE is currently transitioning (starting or stopping). Please wait and try again."));
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldParam);
	if (!World)
	{
		if (WorldParam.Equals(TEXT("pie"), ESearchCase::IgnoreCase))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PIE_NOT_RUNNING"), TEXT("No PIE session running. Use pie-session action:start first."));
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available. Open a level first."));
	}

	TArray<FString> Parts;
	Target.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() < 2)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("Invalid target format. Expected: ActorName.Function or ActorName.Component.Function"));
	}

	const FString ActorName = Parts[0];
	AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
	if (!Actor)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("actor_name"), ActorName);
		Details->SetStringField(TEXT("world"), WorldParam);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), FString::Printf(TEXT("Actor not found: %s"), *ActorName), Details);
	}

	if (Parts.Num() == 2)
	{
		return CallFunctionOnObject(Actor, Parts[1], Arguments);
	}
	if (Parts.Num() == 3)
	{
		const FString ComponentName = Parts[1];
		UActorComponent* Component = FindComponentByName(Actor, ComponentName);
		if (!Component)
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("actor_name"), ActorName);
			Details->SetStringField(TEXT("component_name"), ComponentName);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), FString::Printf(TEXT("Component not found: %s on actor %s"), *ComponentName, *ActorName), Details);
		}

		return CallFunctionOnObject(Component, Parts[2], Arguments);
	}

	return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("Invalid target format. Too many parts."));
}

UActorComponent* UCallFunctionTool::FindComponentByName(AActor* Actor, const FString& ComponentName) const
{
	return FMcpAssetModifier::FindComponentByName(Actor, ComponentName);
}

FMcpToolResult UCallFunctionTool::CallFunctionOnObject(UObject* Object, const FString& FunctionName, const TSharedPtr<FJsonObject>& Arguments)
{
	if (!Object)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("Object is null"));
	}

	UFunction* Function = Object->FindFunction(FName(*FunctionName));
	if (!Function)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("function_name"), FunctionName);
		Details->SetStringField(TEXT("object_name"), Object->GetName());
		Details->SetStringField(TEXT("object_class"), Object->GetClass()->GetName());
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_INVALID_ACTION"),
			FString::Printf(TEXT("Function not found: %s on %s"), *FunctionName, *Object->GetName()),
			Details);
	}

	// Parameter-size safety guard to avoid excessive temporary allocations.
	constexpr int32 MaxParmsSize = 64 * 1024;
	if (Function->ParmsSize > MaxParmsSize)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("function_name"), FunctionName);
		Details->SetNumberField(TEXT("parameter_size"), Function->ParmsSize);
		Details->SetNumberField(TEXT("max_parameter_size"), MaxParmsSize);
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_INVALID_ACTION"),
			FString::Printf(
				TEXT("Function '%s' parameter size (%d bytes) exceeds safety limit (%d bytes)"),
				*FunctionName,
				Function->ParmsSize,
				MaxParmsSize),
			Details);
	}

	uint8* ParamBuffer = static_cast<uint8*>(FMemory::Malloc(Function->ParmsSize));
	FMemory::Memzero(ParamBuffer, Function->ParmsSize);

	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(ParamBuffer);
	}

	if (Arguments->HasField(TEXT("arguments")))
	{
		const TSharedPtr<FJsonObject>* FuncArgs = nullptr;
		if (Arguments->TryGetObjectField(TEXT("arguments"), FuncArgs) && FuncArgs && (*FuncArgs).IsValid())
		{
			for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
			{
				FProperty* Param = *It;
				if (Param->PropertyFlags & CPF_ReturnParm)
				{
					continue;
				}

				const FString ParamName = Param->GetName();
				if ((*FuncArgs)->HasField(ParamName))
				{
					const TSharedPtr<FJsonValue> ParamValue = (*FuncArgs)->TryGetField(ParamName);
					void* ParamAddr = Param->ContainerPtrToValuePtr<void>(ParamBuffer);
					SetPropertyFromJson(ParamAddr, Param, ParamValue);
				}
			}
		}
	}

	Object->ProcessEvent(Function, ParamBuffer);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), TEXT("call-function"));
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetStringField(TEXT("function"), FunctionName);
	Result->SetStringField(TEXT("object"), Object->GetName());
	Result->SetStringField(TEXT("object_class"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("object_path"), Object->GetPathName());

	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		if (Param->PropertyFlags & CPF_ReturnParm)
		{
			const TSharedPtr<FJsonValue> ReturnValue = GetPropertyValue(ParamBuffer, Param);
			if (ReturnValue.IsValid())
			{
				Result->SetField(TEXT("return_value"), ReturnValue);
			}
			break;
		}
	}

	for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(ParamBuffer);
	}
	FMemory::Free(ParamBuffer);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("call-function: Called %s on %s"), *FunctionName, *Object->GetName());
	return FMcpToolResult::StructuredJson(Result);
}

FMcpToolResult UCallFunctionTool::CallGlobalFunction(const FString& BlueprintPath, const FString& FunctionName, const TSharedPtr<FJsonObject>& Arguments)
{
	FString LoadError;
	UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), BlueprintPath);
		const FString ErrorCode = LoadError.Contains(TEXT("expected type"))
			? TEXT("UEBMCP_ASSET_TYPE_MISMATCH")
			: TEXT("UEBMCP_ASSET_NOT_FOUND");
		return FMcpToolResult::StructuredError(ErrorCode, LoadError, Details);
	}

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), BlueprintPath);
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_INVALID_ACTION"),
			FString::Printf(TEXT("Blueprint has no generated class: %s"), *BlueprintPath),
			Details);
	}

	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), BlueprintPath);
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_INVALID_ACTION"),
			FString::Printf(TEXT("Could not get CDO for: %s"), *BlueprintPath),
			Details);
	}

	return CallFunctionOnObject(CDO, FunctionName, Arguments);
}

TSharedPtr<FJsonValue> UCallFunctionTool::GetPropertyValue(void* Container, FProperty* Property) const
{
	if (!Property || !Container) return nullptr;

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);

	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			double Value = 0;
			NumProp->GetValue_InContainer(Container, &Value);
			return MakeShareable(new FJsonValueNumber(Value));
		}
		else
		{
			int64 Value = 0;
			NumProp->GetValue_InContainer(Container, &Value);
			return MakeShareable(new FJsonValueNumber(static_cast<double>(Value)));
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShareable(new FJsonValueBoolean(BoolProp->GetPropertyValue(ValuePtr)));
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(StrProp->GetPropertyValue(ValuePtr)));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(NameProp->GetPropertyValue(ValuePtr).ToString()));
	}

	// Fallback: export as string
	FString ExportedText;
	Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShareable(new FJsonValueString(ExportedText));
}

bool UCallFunctionTool::SetPropertyFromJson(void* PropertyAddr, FProperty* Property, const TSharedPtr<FJsonValue>& JsonValue) const
{
	if (!Property || !PropertyAddr || !JsonValue.IsValid()) return false;

	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			double Value = JsonValue->AsNumber();
			NumProp->SetFloatingPointPropertyValue(PropertyAddr, Value);
		}
		else
		{
			int64 Value = static_cast<int64>(JsonValue->AsNumber());
			NumProp->SetIntPropertyValue(PropertyAddr, Value);
		}
		return true;
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		BoolProp->SetPropertyValue(PropertyAddr, JsonValue->AsBool());
		return true;
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		StrProp->SetPropertyValue(PropertyAddr, JsonValue->AsString());
		return true;
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		NameProp->SetPropertyValue(PropertyAddr, FName(*JsonValue->AsString()));
		return true;
	}

	return false;
}