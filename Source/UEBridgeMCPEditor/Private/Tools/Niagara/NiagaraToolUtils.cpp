// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Niagara/NiagaraToolUtils.h"

#include "Utils/McpAssetModifier.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> Vector2ToJsonArray(const FVector2f& Value)
	{
		return {
			MakeShareable(new FJsonValueNumber(Value.X)),
			MakeShareable(new FJsonValueNumber(Value.Y))
		};
	}

	TArray<TSharedPtr<FJsonValue>> Vector3ToJsonArray(const FVector3f& Value)
	{
		return {
			MakeShareable(new FJsonValueNumber(Value.X)),
			MakeShareable(new FJsonValueNumber(Value.Y)),
			MakeShareable(new FJsonValueNumber(Value.Z))
		};
	}

	TArray<TSharedPtr<FJsonValue>> Vector4ToJsonArray(const FVector4f& Value)
	{
		return {
			MakeShareable(new FJsonValueNumber(Value.X)),
			MakeShareable(new FJsonValueNumber(Value.Y)),
			MakeShareable(new FJsonValueNumber(Value.Z)),
			MakeShareable(new FJsonValueNumber(Value.W))
		};
	}

	TSharedPtr<FJsonObject> ColorToJsonObject(const FLinearColor& Value)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("r"), Value.R);
		Object->SetNumberField(TEXT("g"), Value.G);
		Object->SetNumberField(TEXT("b"), Value.B);
		Object->SetNumberField(TEXT("a"), Value.A);
		return Object;
	}

	bool TryReadNumber(const TSharedPtr<FJsonValue>& Value, double& OutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Number)
		{
			OutValue = Value->AsNumber();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			return LexTryParseString(OutValue, *Value->AsString());
		}

		return false;
	}

	bool TryReadBool(const TSharedPtr<FJsonValue>& Value, bool& OutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Boolean)
		{
			OutValue = Value->AsBool();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			const FString StringValue = Value->AsString().ToLower();
			if (StringValue == TEXT("true") || StringValue == TEXT("1") || StringValue == TEXT("yes"))
			{
				OutValue = true;
				return true;
			}
			if (StringValue == TEXT("false") || StringValue == TEXT("0") || StringValue == TEXT("no"))
			{
				OutValue = false;
				return true;
			}
		}

		return false;
	}

	bool TryReadVectorFromArray(const TSharedPtr<FJsonValue>& Value, int32 ExpectedCount, TArray<double>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(ArrayValue) || !ArrayValue || ArrayValue->Num() < ExpectedCount)
		{
			return false;
		}

		OutValues.Reset(ExpectedCount);
		for (int32 Index = 0; Index < ExpectedCount; ++Index)
		{
			double NumberValue = 0.0;
			if (!TryReadNumber((*ArrayValue)[Index], NumberValue))
			{
				return false;
			}
			OutValues.Add(NumberValue);
		}
		return true;
	}

	bool TryReadVectorFromObject(
		const TSharedPtr<FJsonValue>& Value,
		const TArray<FString>& FieldNames,
		TArray<double>& OutValues)
	{
		const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(ObjectValue) || !ObjectValue || !(*ObjectValue).IsValid())
		{
			return false;
		}

		OutValues.Reset(FieldNames.Num());
		for (const FString& FieldName : FieldNames)
		{
			double NumberValue = 0.0;
			if (!(*ObjectValue)->TryGetNumberField(FieldName, NumberValue))
			{
				return false;
			}
			OutValues.Add(NumberValue);
		}
		return true;
	}

	bool TryReadVector2(const TSharedPtr<FJsonValue>& Value, FVector2f& OutVector)
	{
		TArray<double> Values;
		if (!TryReadVectorFromArray(Value, 2, Values) &&
			!TryReadVectorFromObject(Value, { TEXT("x"), TEXT("y") }, Values))
		{
			return false;
		}

		OutVector = FVector2f(static_cast<float>(Values[0]), static_cast<float>(Values[1]));
		return true;
	}

	bool TryReadVector3(const TSharedPtr<FJsonValue>& Value, FVector3f& OutVector)
	{
		TArray<double> Values;
		if (!TryReadVectorFromArray(Value, 3, Values) &&
			!TryReadVectorFromObject(Value, { TEXT("x"), TEXT("y"), TEXT("z") }, Values))
		{
			return false;
		}

		OutVector = FVector3f(static_cast<float>(Values[0]), static_cast<float>(Values[1]), static_cast<float>(Values[2]));
		return true;
	}

	bool TryReadVector4(const TSharedPtr<FJsonValue>& Value, FVector4f& OutVector)
	{
		TArray<double> Values;
		if (!TryReadVectorFromArray(Value, 4, Values) &&
			!TryReadVectorFromObject(Value, { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("w") }, Values) &&
			!TryReadVectorFromObject(Value, { TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a") }, Values))
		{
			return false;
		}

		OutVector = FVector4f(
			static_cast<float>(Values[0]),
			static_cast<float>(Values[1]),
			static_cast<float>(Values[2]),
			static_cast<float>(Values[3]));
		return true;
	}

	bool TryReadColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
	{
		FVector4f VectorValue;
		if (!TryReadVector4(Value, VectorValue))
		{
			return false;
		}

		OutColor = FLinearColor(VectorValue.X, VectorValue.Y, VectorValue.Z, VectorValue.W);
		return true;
	}

	bool TypeMatches(const FNiagaraTypeDefinition& Left, const FNiagaraTypeDefinition& Right)
	{
		return Left == Right || Left.IsSameBaseDefinition(Right);
	}

	bool StoreValueToJson(
		const FNiagaraParameterStore& Store,
		const FNiagaraVariable& Variable,
		TSharedPtr<FJsonValue>& OutValue)
	{
		const FNiagaraTypeDefinition& Type = Variable.GetType();
		if (Store.IndexOf(Variable) == INDEX_NONE)
		{
			return false;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetBoolDef()))
		{
			const FNiagaraBool BoolValue = Store.GetParameterValue<FNiagaraBool>(Variable);
			OutValue = MakeShareable(new FJsonValueBoolean(static_cast<bool>(BoolValue)));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetIntDef()))
		{
			OutValue = MakeShareable(new FJsonValueNumber(Store.GetParameterValue<int32>(Variable)));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetFloatDef()))
		{
			OutValue = MakeShareable(new FJsonValueNumber(Store.GetParameterValue<float>(Variable)));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec2Def()))
		{
			OutValue = MakeShareable(new FJsonValueArray(Vector2ToJsonArray(Store.GetParameterValue<FVector2f>(Variable))));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec3Def()))
		{
			OutValue = MakeShareable(new FJsonValueArray(Vector3ToJsonArray(Store.GetParameterValue<FVector3f>(Variable))));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetPositionDef()))
		{
			const FNiagaraPosition PositionValue = Store.GetParameterValue<FNiagaraPosition>(Variable);
			OutValue = MakeShareable(new FJsonValueArray(Vector3ToJsonArray(FVector3f(PositionValue.X, PositionValue.Y, PositionValue.Z))));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec4Def()))
		{
			OutValue = MakeShareable(new FJsonValueArray(Vector4ToJsonArray(Store.GetParameterValue<FVector4f>(Variable))));
			return true;
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetColorDef()))
		{
			OutValue = MakeShareable(new FJsonValueObject(ColorToJsonObject(Store.GetParameterValue<FLinearColor>(Variable))));
			return true;
		}

		return false;
	}
}

namespace NiagaraToolUtils
{
	FString NormalizeUserParameterName(const FString& Name)
	{
		FString TrimmedName = Name;
		TrimmedName.TrimStartAndEndInline();
		if (TrimmedName.StartsWith(TEXT("User."), ESearchCase::IgnoreCase))
		{
			return TrimmedName;
		}
		return FString::Printf(TEXT("User.%s"), *TrimmedName);
	}

	FString StripUserNamespace(const FString& Name)
	{
		if (Name.StartsWith(TEXT("User."), ESearchCase::IgnoreCase))
		{
			return Name.RightChop(5);
		}
		return Name;
	}

	bool TryResolveType(const FString& TypeName, FNiagaraTypeDefinition& OutType, FString& OutError)
	{
		const FString LowerType = TypeName.ToLower();
		if (LowerType == TEXT("bool") || LowerType == TEXT("boolean"))
		{
			OutType = FNiagaraTypeDefinition::GetBoolDef();
			return true;
		}
		if (LowerType == TEXT("int") || LowerType == TEXT("int32") || LowerType == TEXT("integer"))
		{
			OutType = FNiagaraTypeDefinition::GetIntDef();
			return true;
		}
		if (LowerType == TEXT("float") || LowerType == TEXT("scalar") || LowerType == TEXT("number"))
		{
			OutType = FNiagaraTypeDefinition::GetFloatDef();
			return true;
		}
		if (LowerType == TEXT("vec2") || LowerType == TEXT("vector2") || LowerType == TEXT("vector2d"))
		{
			OutType = FNiagaraTypeDefinition::GetVec2Def();
			return true;
		}
		if (LowerType == TEXT("vec3") || LowerType == TEXT("vector") || LowerType == TEXT("vector3"))
		{
			OutType = FNiagaraTypeDefinition::GetVec3Def();
			return true;
		}
		if (LowerType == TEXT("position"))
		{
			OutType = FNiagaraTypeDefinition::GetPositionDef();
			return true;
		}
		if (LowerType == TEXT("vec4") || LowerType == TEXT("vector4"))
		{
			OutType = FNiagaraTypeDefinition::GetVec4Def();
			return true;
		}
		if (LowerType == TEXT("color") || LowerType == TEXT("linearcolor") || LowerType == TEXT("linear_color"))
		{
			OutType = FNiagaraTypeDefinition::GetColorDef();
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported Niagara user parameter type '%s'"), *TypeName);
		return false;
	}

	FString TypeToString(const FNiagaraTypeDefinition& Type)
	{
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetBoolDef()))
		{
			return TEXT("bool");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetIntDef()))
		{
			return TEXT("int32");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetFloatDef()))
		{
			return TEXT("float");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec2Def()))
		{
			return TEXT("vector2");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec3Def()))
		{
			return TEXT("vector3");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetPositionDef()))
		{
			return TEXT("position");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec4Def()))
		{
			return TEXT("vector4");
		}
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetColorDef()))
		{
			return TEXT("color");
		}

		return Type.GetName();
	}

	bool TryFindUserParameter(
		const FNiagaraUserRedirectionParameterStore& Store,
		const FString& Name,
		FNiagaraVariable& OutVariable)
	{
		const FString RequestedFullName = NormalizeUserParameterName(Name);
		const FString RequestedShortName = StripUserNamespace(Name);

		TArray<FNiagaraVariable> Parameters;
		Store.GetUserParameters(Parameters);
		for (const FNiagaraVariable& Parameter : Parameters)
		{
			const FString ParameterName = Parameter.GetName().ToString();
			if (ParameterName.Equals(RequestedFullName, ESearchCase::IgnoreCase) ||
				StripUserNamespace(ParameterName).Equals(RequestedShortName, ESearchCase::IgnoreCase))
			{
				OutVariable = Parameter;
				return true;
			}
		}

		return false;
	}

	TSharedPtr<FJsonObject> SerializeVariable(const FNiagaraVariable& Variable, const FNiagaraParameterStore* Store)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		const FString FullName = Variable.GetName().ToString();
		Object->SetStringField(TEXT("name"), FullName);
		Object->SetStringField(TEXT("display_name"), StripUserNamespace(FullName));
		Object->SetStringField(TEXT("type"), TypeToString(Variable.GetType()));
		Object->SetStringField(TEXT("type_name"), Variable.GetType().GetName());
		Object->SetNumberField(TEXT("size_bytes"), Variable.GetSizeInBytes());
		Object->SetBoolField(TEXT("is_data_interface"), Variable.IsDataInterface());
		Object->SetBoolField(TEXT("is_uobject"), Variable.IsUObject());

		if (Store)
		{
			TSharedPtr<FJsonValue> Value;
			if (StoreValueToJson(*Store, Variable, Value) && Value.IsValid())
			{
				Object->SetField(TEXT("value"), Value);
			}
		}

		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SerializeUserParameters(const FNiagaraUserRedirectionParameterStore& Store)
	{
		TArray<FNiagaraVariable> Parameters;
		Store.GetUserParameters(Parameters);

		Parameters.Sort([](const FNiagaraVariable& Left, const FNiagaraVariable& Right)
		{
			return Left.GetName().LexicalLess(Right.GetName());
		});

		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FNiagaraVariable& Parameter : Parameters)
		{
			Result.Add(MakeShareable(new FJsonValueObject(SerializeVariable(Parameter, &Store))));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> SerializeEmitterHandle(const FNiagaraEmitterHandle& Handle, bool bIncludeRenderers)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Handle.GetName().ToString());
		Object->SetStringField(TEXT("id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("id_name"), Handle.GetIdName().ToString());
		Object->SetBoolField(TEXT("valid"), Handle.IsValid());
		Object->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
		Object->SetBoolField(TEXT("allowed_by_scalability"), Handle.IsAllowedByScalability());
		Object->SetStringField(TEXT("unique_instance_name"), Handle.GetUniqueInstanceName());

		if (const UNiagaraEmitterBase* EmitterBase = Handle.GetEmitterBase())
		{
			Object->SetStringField(TEXT("emitter_class"), EmitterBase->GetClass()->GetName());
			Object->SetStringField(TEXT("emitter_path"), EmitterBase->GetPathName());
		}

		if (bIncludeRenderers)
		{
			TArray<TSharedPtr<FJsonValue>> Renderers;
			Handle.ForEachEnabledRendererWithIndex([&Renderers](const UNiagaraRendererProperties* Renderer, int32 Index)
			{
				TSharedPtr<FJsonObject> RendererObject = MakeShareable(new FJsonObject);
				RendererObject->SetNumberField(TEXT("index"), Index);
				if (Renderer)
				{
					RendererObject->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
					RendererObject->SetStringField(TEXT("name"), Renderer->GetName());
				}
				Renderers.Add(MakeShareable(new FJsonValueObject(RendererObject)));
			});
			Object->SetArrayField(TEXT("enabled_renderers"), Renderers);
			Object->SetNumberField(TEXT("enabled_renderer_count"), Renderers.Num());
		}

		return Object;
	}

	TSharedPtr<FJsonObject> SerializeSystemSummary(UNiagaraSystem* System, bool bIncludeEmitters, bool bIncludeUserParameters)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!System)
		{
			return Object;
		}

		Object->SetStringField(TEXT("asset_path"), System->GetPathName());
		Object->SetStringField(TEXT("name"), System->GetName());
		Object->SetStringField(TEXT("class"), System->GetClass()->GetName());
		Object->SetBoolField(TEXT("ready_to_run"), System->IsReadyToRun());
		Object->SetBoolField(TEXT("needs_compile"), System->NeedsRequestCompile());
		Object->SetNumberField(TEXT("warmup_time"), System->GetWarmupTime());

		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		Object->SetNumberField(TEXT("emitter_count"), Handles.Num());
		if (bIncludeEmitters)
		{
			TArray<TSharedPtr<FJsonValue>> Emitters;
			for (const FNiagaraEmitterHandle& Handle : Handles)
			{
				Emitters.Add(MakeShareable(new FJsonValueObject(SerializeEmitterHandle(Handle, true))));
			}
			Object->SetArrayField(TEXT("emitters"), Emitters);
		}

		if (bIncludeUserParameters)
		{
			const FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			const TArray<TSharedPtr<FJsonValue>> UserParameters = SerializeUserParameters(Store);
			Object->SetArrayField(TEXT("user_parameters"), UserParameters);
			Object->SetNumberField(TEXT("user_parameter_count"), UserParameters.Num());
		}

		return Object;
	}

	bool SetParameterStoreValue(
		FNiagaraParameterStore& Store,
		const FNiagaraVariable& Variable,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError)
	{
		const FNiagaraTypeDefinition& Type = Variable.GetType();
		if (TypeMatches(Type, FNiagaraTypeDefinition::GetBoolDef()))
		{
			bool BoolValue = false;
			if (!TryReadBool(Value, BoolValue))
			{
				OutError = TEXT("Expected boolean value");
				return false;
			}
			return Store.SetParameterValue(FNiagaraBool(BoolValue), Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetIntDef()))
		{
			double NumberValue = 0.0;
			if (!TryReadNumber(Value, NumberValue))
			{
				OutError = TEXT("Expected integer value");
				return false;
			}
			return Store.SetParameterValue(static_cast<int32>(NumberValue), Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetFloatDef()))
		{
			double NumberValue = 0.0;
			if (!TryReadNumber(Value, NumberValue))
			{
				OutError = TEXT("Expected float value");
				return false;
			}
			return Store.SetParameterValue(static_cast<float>(NumberValue), Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec2Def()))
		{
			FVector2f VectorValue = FVector2f::ZeroVector;
			if (!TryReadVector2(Value, VectorValue))
			{
				OutError = TEXT("Expected vector2 value as [x,y] or {x,y}");
				return false;
			}
			return Store.SetParameterValue(VectorValue, Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec3Def()))
		{
			FVector3f VectorValue = FVector3f::ZeroVector;
			if (!TryReadVector3(Value, VectorValue))
			{
				OutError = TEXT("Expected vector3 value as [x,y,z] or {x,y,z}");
				return false;
			}
			return Store.SetParameterValue(VectorValue, Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetPositionDef()))
		{
			FVector3f VectorValue = FVector3f::ZeroVector;
			if (!TryReadVector3(Value, VectorValue))
			{
				OutError = TEXT("Expected position value as [x,y,z] or {x,y,z}");
				return false;
			}
			return Store.SetParameterValue(FNiagaraPosition(VectorValue), Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetVec4Def()))
		{
			FVector4f VectorValue = FVector4f(0, 0, 0, 0);
			if (!TryReadVector4(Value, VectorValue))
			{
				OutError = TEXT("Expected vector4 value as [x,y,z,w] or {x,y,z,w}");
				return false;
			}
			return Store.SetParameterValue(VectorValue, Variable, true);
		}

		if (TypeMatches(Type, FNiagaraTypeDefinition::GetColorDef()))
		{
			FLinearColor ColorValue = FLinearColor::White;
			if (!TryReadColor(Value, ColorValue))
			{
				OutError = TEXT("Expected color value as [r,g,b,a] or {r,g,b,a}");
				return false;
			}
			return Store.SetParameterValue(ColorValue, Variable, true);
		}

		OutError = FString::Printf(TEXT("Setting values for Niagara type '%s' is not supported in v1"), *TypeToString(Type));
		return false;
	}

	bool ApplyComponentOverride(
		UNiagaraComponent* Component,
		const FString& Name,
		const FString& Type,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError)
	{
		if (!Component)
		{
			OutError = TEXT("Niagara component is required");
			return false;
		}

		const FName ParameterName(*NormalizeUserParameterName(Name));
		FNiagaraTypeDefinition TypeDefinition;
		if (!TryResolveType(Type, TypeDefinition, OutError))
		{
			return false;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetBoolDef()))
		{
			bool BoolValue = false;
			if (!TryReadBool(Value, BoolValue))
			{
				OutError = TEXT("Expected boolean value");
				return false;
			}
			Component->SetVariableBool(ParameterName, BoolValue);
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetIntDef()))
		{
			double NumberValue = 0.0;
			if (!TryReadNumber(Value, NumberValue))
			{
				OutError = TEXT("Expected integer value");
				return false;
			}
			Component->SetVariableInt(ParameterName, static_cast<int32>(NumberValue));
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetFloatDef()))
		{
			double NumberValue = 0.0;
			if (!TryReadNumber(Value, NumberValue))
			{
				OutError = TEXT("Expected float value");
				return false;
			}
			Component->SetVariableFloat(ParameterName, static_cast<float>(NumberValue));
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetVec2Def()))
		{
			FVector2f VectorValue = FVector2f::ZeroVector;
			if (!TryReadVector2(Value, VectorValue))
			{
				OutError = TEXT("Expected vector2 value");
				return false;
			}
			Component->SetVariableVec2(ParameterName, FVector2D(VectorValue.X, VectorValue.Y));
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetVec3Def()))
		{
			FVector3f VectorValue = FVector3f::ZeroVector;
			if (!TryReadVector3(Value, VectorValue))
			{
				OutError = TEXT("Expected vector3 value");
				return false;
			}
			Component->SetVariableVec3(ParameterName, FVector(VectorValue.X, VectorValue.Y, VectorValue.Z));
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetPositionDef()))
		{
			FVector3f VectorValue = FVector3f::ZeroVector;
			if (!TryReadVector3(Value, VectorValue))
			{
				OutError = TEXT("Expected position value");
				return false;
			}
			Component->SetVariablePosition(ParameterName, FVector(VectorValue.X, VectorValue.Y, VectorValue.Z));
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetVec4Def()))
		{
			FVector4f VectorValue = FVector4f(0, 0, 0, 0);
			if (!TryReadVector4(Value, VectorValue))
			{
				OutError = TEXT("Expected vector4 value");
				return false;
			}
			Component->SetVariableVec4(ParameterName, FVector4(VectorValue.X, VectorValue.Y, VectorValue.Z, VectorValue.W));
			return true;
		}

		if (TypeMatches(TypeDefinition, FNiagaraTypeDefinition::GetColorDef()))
		{
			FLinearColor ColorValue = FLinearColor::White;
			if (!TryReadColor(Value, ColorValue))
			{
				OutError = TEXT("Expected color value");
				return false;
			}
			Component->SetVariableLinearColor(ParameterName, ColorValue);
			return true;
		}

		OutError = FString::Printf(TEXT("Component overrides for Niagara type '%s' are not supported in v1"), *Type);
		return false;
	}

	TSharedPtr<FJsonObject> CompileSystem(UNiagaraSystem* System, bool bForce)
	{
		TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
		CompileObject->SetBoolField(TEXT("requested"), false);
		CompileObject->SetBoolField(TEXT("completed"), false);
		CompileObject->SetBoolField(TEXT("ready_to_run"), false);
		if (!System)
		{
			CompileObject->SetStringField(TEXT("error"), TEXT("Niagara system is null"));
			return CompileObject;
		}

		const bool bRequested = System->RequestCompile(bForce);
		System->WaitForCompilationComplete(false, false);
		const bool bCompleted = System->PollForCompilationComplete(true);

		CompileObject->SetBoolField(TEXT("requested"), bRequested);
		CompileObject->SetBoolField(TEXT("completed"), bCompleted);
		CompileObject->SetBoolField(TEXT("ready_to_run"), System->IsReadyToRun());
		CompileObject->SetBoolField(TEXT("needs_compile"), System->NeedsRequestCompile());
		return CompileObject;
	}

	bool SaveAsset(UObject* Asset, TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Asset, false, SaveError))
		{
			OutWarnings.Add(MakeShareable(new FJsonValueString(SaveError)));
			return false;
		}
		return true;
	}
}
