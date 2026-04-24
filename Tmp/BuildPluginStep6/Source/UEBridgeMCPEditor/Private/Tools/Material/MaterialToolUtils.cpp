// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/MaterialToolUtils.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"

#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "SceneTypes.h"

namespace
{
	TSharedPtr<FJsonObject> MakePositionJson(const int32 X, const int32 Y)
	{
		TSharedPtr<FJsonObject> PositionObject = MakeShareable(new FJsonObject);
		PositionObject->SetNumberField(TEXT("x"), X);
		PositionObject->SetNumberField(TEXT("y"), Y);
		return PositionObject;
	}

	const TMap<FString, EMaterialProperty>& GetMaterialPropertyAliasMap()
	{
		static const TMap<FString, EMaterialProperty> Aliases = {
			{TEXT("emissive"), MP_EmissiveColor},
			{TEXT("emissivecolor"), MP_EmissiveColor},
			{TEXT("opacity"), MP_Opacity},
			{TEXT("opacitymask"), MP_OpacityMask},
			{TEXT("basecolor"), MP_BaseColor},
			{TEXT("diffuse"), MP_BaseColor},
			{TEXT("metallic"), MP_Metallic},
			{TEXT("specular"), MP_Specular},
			{TEXT("roughness"), MP_Roughness},
			{TEXT("anisotropy"), MP_Anisotropy},
			{TEXT("normal"), MP_Normal},
			{TEXT("tangent"), MP_Tangent},
			{TEXT("worldpositionoffset"), MP_WorldPositionOffset},
			{TEXT("subsurfacecolor"), MP_SubsurfaceColor},
			{TEXT("clearcoat"), MP_CustomData0},
			{TEXT("clearcoatroughness"), MP_CustomData1},
			{TEXT("ambientocclusion"), MP_AmbientOcclusion},
			{TEXT("refraction"), MP_Refraction},
			{TEXT("pixeldepthoffset"), MP_PixelDepthOffset},
			{TEXT("materialattributes"), MP_MaterialAttributes},
			{TEXT("customizeduv0"), MP_CustomizedUVs0},
			{TEXT("customizeduv1"), MP_CustomizedUVs1},
			{TEXT("customizeduv2"), MP_CustomizedUVs2},
			{TEXT("customizeduv3"), MP_CustomizedUVs3},
			{TEXT("customizeduv4"), MP_CustomizedUVs4},
			{TEXT("customizeduv5"), MP_CustomizedUVs5},
			{TEXT("customizeduv6"), MP_CustomizedUVs6},
			{TEXT("customizeduv7"), MP_CustomizedUVs7},
			{TEXT("frontmaterial"), MP_FrontMaterial},
		};
		return Aliases;
	}
}

namespace MaterialToolUtils
{
	TSharedPtr<FJsonObject> BuildGraphOverview(UMaterial* Material, const bool bIncludePositions)
	{
		if (!Material)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> OverviewObject = MakeShareable(new FJsonObject);

		TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (!Expression)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ExpressionObject = MakeShareable(new FJsonObject);
			ExpressionObject->SetStringField(TEXT("name"), Expression->GetName());
			ExpressionObject->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
			ExpressionObject->SetStringField(TEXT("description"), Expression->GetDescription());
			if (bIncludePositions)
			{
				ExpressionObject->SetObjectField(
					TEXT("position"),
					MakePositionJson(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));
			}

			ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExpressionObject)));
		}

		TArray<TSharedPtr<FJsonValue>> PropertyLinksArray;
		for (int32 PropertyIndex = 0; PropertyIndex < static_cast<int32>(MP_MAX); ++PropertyIndex)
		{
			const EMaterialProperty MaterialProperty = static_cast<EMaterialProperty>(PropertyIndex);
			FExpressionInput* PropertyInput = Material->GetExpressionInputForProperty(MaterialProperty);
			if (!PropertyInput || !PropertyInput->Expression)
			{
				continue;
			}

			TSharedPtr<FJsonObject> LinkObject = MakeShareable(new FJsonObject);
			LinkObject->SetStringField(TEXT("material_property"), MaterialPropertyToString(MaterialProperty));
			LinkObject->SetStringField(TEXT("expression_name"), PropertyInput->Expression->GetName());
			LinkObject->SetNumberField(TEXT("output_index"), PropertyInput->OutputIndex);

			TArray<FExpressionOutput>& Outputs = PropertyInput->Expression->GetOutputs();
			if (Outputs.IsValidIndex(PropertyInput->OutputIndex))
			{
				LinkObject->SetStringField(TEXT("output_name"), Outputs[PropertyInput->OutputIndex].OutputName.ToString());
			}

			PropertyLinksArray.Add(MakeShareable(new FJsonValueObject(LinkObject)));
		}

		OverviewObject->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());
		OverviewObject->SetArrayField(TEXT("expressions"), ExpressionsArray);
		OverviewObject->SetArrayField(TEXT("material_property_links"), PropertyLinksArray);
		return OverviewObject;
	}

	bool ResolveExpressionClass(const FString& RequestedClassName, UClass*& OutExpressionClass, FString& OutError)
	{
		OutExpressionClass = nullptr;

		const TArray<FString> Candidates = {
			RequestedClassName,
			RequestedClassName.StartsWith(TEXT("MaterialExpression")) ? RequestedClassName : FString::Printf(TEXT("MaterialExpression%s"), *RequestedClassName),
			RequestedClassName.StartsWith(TEXT("U")) ? RequestedClassName : FString::Printf(TEXT("U%s"), *RequestedClassName)
		};

		for (const FString& Candidate : Candidates)
		{
			FString ResolveError;
			UClass* ResolvedClass = FMcpPropertySerializer::ResolveClass(Candidate, ResolveError);
			if (!ResolvedClass)
			{
				continue;
			}

			if (!ResolvedClass->IsChildOf(UMaterialExpression::StaticClass()))
			{
				OutError = FString::Printf(TEXT("Class '%s' is not a UMaterialExpression subclass"), *Candidate);
				return false;
			}

			OutExpressionClass = ResolvedClass;
			return true;
		}

		OutError = FString::Printf(TEXT("Material expression class not found: %s"), *RequestedClassName);
		return false;
	}

	bool ApplyObjectProperties(UObject* Object, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
	{
		if (!Object || !Properties.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
		{
			FProperty* Property = Object->GetClass()->FindPropertyByName(*Pair.Key);
			void* Container = Object;
			if (!Property)
			{
				FString FindError;
				if (!FMcpAssetModifier::FindPropertyByPath(Object, Pair.Key, Property, Container, FindError))
				{
					OutError = FString::Printf(TEXT("Failed to find property '%s': %s"), *Pair.Key, *FindError);
					return false;
				}
			}

			FString SetError;
			if (!FMcpPropertySerializer::DeserializePropertyValue(Property, Container, Pair.Value, SetError))
			{
				OutError = FString::Printf(TEXT("Failed to set property '%s': %s"), *Pair.Key, *SetError);
				return false;
			}
		}

		return true;
	}

	UMaterialExpression* FindExpressionByName(UMaterial* Material, const FString& ExpressionName)
	{
		if (!Material || ExpressionName.IsEmpty())
		{
			return nullptr;
		}

		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (Expression && Expression->GetName().Equals(ExpressionName, ESearchCase::IgnoreCase))
			{
				return Expression;
			}
		}

		return nullptr;
	}

	bool ResolveExpressionInput(UMaterialExpression* Expression, const FString& RequestedInputName, FExpressionInput*& OutInput, FString& OutError)
	{
		OutInput = nullptr;
		if (!Expression)
		{
			OutError = TEXT("Expression is null");
			return false;
		}

		if (RequestedInputName.IsEmpty())
		{
			OutInput = Expression->GetInput(0);
			if (!OutInput)
			{
				OutError = TEXT("Expression has no inputs");
				return false;
			}
			return true;
		}

		for (int32 InputIndex = 0; ; ++InputIndex)
		{
			FExpressionInput* Candidate = Expression->GetInput(InputIndex);
			if (!Candidate)
			{
				break;
			}

			if (Expression->GetInputName(InputIndex).ToString().Equals(RequestedInputName, ESearchCase::IgnoreCase))
			{
				OutInput = Candidate;
				return true;
			}
		}

		if (RequestedInputName.Equals(TEXT("A"), ESearchCase::IgnoreCase))
		{
			OutInput = Expression->GetInput(0);
		}
		else if (RequestedInputName.Equals(TEXT("B"), ESearchCase::IgnoreCase))
		{
			OutInput = Expression->GetInput(1);
		}

		if (OutInput)
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Input '%s' was not found on expression '%s'"), *RequestedInputName, *Expression->GetName());
		return false;
	}

	bool ResolveExpressionOutputName(UMaterialExpression* Expression, const FString& RequestedOutput, FString& OutOutputName, int32& OutOutputIndex, FString& OutError)
	{
		OutOutputName.Reset();
		OutOutputIndex = 0;

		if (!Expression)
		{
			OutError = TEXT("Expression is null");
			return false;
		}

		TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
		if (Outputs.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Expression '%s' has no outputs"), *Expression->GetName());
			return false;
		}

		if (RequestedOutput.IsEmpty())
		{
			OutOutputName = Outputs[0].OutputName.ToString();
			return true;
		}

		if (RequestedOutput.IsNumeric())
		{
			const int32 RequestedIndex = FCString::Atoi(*RequestedOutput);
			if (!Outputs.IsValidIndex(RequestedIndex))
			{
				OutError = FString::Printf(TEXT("Output index %d is out of range for expression '%s'"), RequestedIndex, *Expression->GetName());
				return false;
			}

			OutOutputIndex = RequestedIndex;
			OutOutputName = Outputs[RequestedIndex].OutputName.ToString();
			return true;
		}

		for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
		{
			if (Outputs[OutputIndex].OutputName.ToString().Equals(RequestedOutput, ESearchCase::IgnoreCase))
			{
				OutOutputIndex = OutputIndex;
				OutOutputName = Outputs[OutputIndex].OutputName.ToString();
				return true;
			}
		}

		OutError = FString::Printf(TEXT("Output '%s' was not found on expression '%s'"), *RequestedOutput, *Expression->GetName());
		return false;
	}

	bool TryParseMaterialProperty(const FString& Value, EMaterialProperty& OutProperty)
	{
		if (Value.IsEmpty())
		{
			return false;
		}

		const FString Normalized = Value.Replace(TEXT(" "), TEXT("")).Replace(TEXT("-"), TEXT("")).ToLower();
		if (const EMaterialProperty* AliasValue = GetMaterialPropertyAliasMap().Find(Normalized))
		{
			OutProperty = *AliasValue;
			return true;
		}

		if (UEnum* PropertyEnum = StaticEnum<EMaterialProperty>())
		{
			const int64 EnumValue = PropertyEnum->GetValueByNameString(Value);
			if (EnumValue != INDEX_NONE)
			{
				OutProperty = static_cast<EMaterialProperty>(EnumValue);
				return true;
			}

			const FString PrefixedValue = Value.StartsWith(TEXT("MP_")) ? Value : FString::Printf(TEXT("MP_%s"), *Value);
			const int64 PrefixedEnumValue = PropertyEnum->GetValueByNameString(PrefixedValue);
			if (PrefixedEnumValue != INDEX_NONE)
			{
				OutProperty = static_cast<EMaterialProperty>(PrefixedEnumValue);
				return true;
			}
		}

		return false;
	}

	FString MaterialPropertyToString(const EMaterialProperty Property)
	{
		if (UEnum* PropertyEnum = StaticEnum<EMaterialProperty>())
		{
			FString Name = PropertyEnum->GetNameStringByValue(static_cast<int64>(Property));
			if (Name.StartsWith(TEXT("MP_")))
			{
				Name.RightChopInline(3, EAllowShrinking::No);
			}
			return Name;
		}

		return TEXT("Unknown");
	}
}
