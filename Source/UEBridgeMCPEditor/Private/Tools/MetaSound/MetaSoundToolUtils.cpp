// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/MetaSound/MetaSoundToolUtils.h"

#include "Utils/McpAssetModifier.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "Modules/ModuleManager.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> StringArrayToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Value : Values)
		{
			Result.Add(MakeShareable(new FJsonValueString(Value)));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> GuidObject(const FGuid& Guid)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetBoolField(TEXT("valid"), Guid.IsValid());
		return Object;
	}

	FString NormalizeTypeName(const FString& TypeName)
	{
		FString Trimmed = TypeName;
		Trimmed.TrimStartAndEndInline();
		return Trimmed.ToLower();
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
			const FString Lower = Value->AsString().ToLower();
			if (Lower == TEXT("true") || Lower == TEXT("1") || Lower == TEXT("yes"))
			{
				OutValue = true;
				return true;
			}
			if (Lower == TEXT("false") || Lower == TEXT("0") || Lower == TEXT("no"))
			{
				OutValue = false;
				return true;
			}
		}
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> BoolArrayToJson(const TArray<bool>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (bool Value : Values)
		{
			Result.Add(MakeShareable(new FJsonValueBoolean(Value)));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> IntArrayToJson(const TArray<int32>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (int32 Value : Values)
		{
			Result.Add(MakeShareable(new FJsonValueNumber(Value)));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> FloatArrayToJson(const TArray<float>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (float Value : Values)
		{
			Result.Add(MakeShareable(new FJsonValueNumber(Value)));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> ObjectArrayToJson(const TArray<UObject*>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const UObject* Value : Values)
		{
			Result.Add(MakeShareable(new FJsonValueString(MetaSoundToolUtils::ObjectPath(Value))));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> VersionSetToJson(const TSet<FMetasoundFrontendVersion>& Versions)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FMetasoundFrontendVersion& Version : Versions)
		{
			TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
			Object->SetStringField(TEXT("name"), Version.Name.ToString());
			Object->SetNumberField(TEXT("major"), Version.Number.Major);
			Object->SetNumberField(TEXT("minor"), Version.Number.Minor);
			Result.Add(MakeShareable(new FJsonValueObject(Object)));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> SerializeClassInput(const FMetasoundFrontendClassInput& Input)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Input.Name.ToString());
		Object->SetStringField(TEXT("type"), Input.TypeName.ToString());
		Object->SetStringField(TEXT("vertex_id"), Input.VertexID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("node_id"), Input.NodeID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetNumberField(TEXT("default_count"), Input.GetDefaults().Num());

		if (const FMetasoundFrontendLiteral* DefaultLiteral = Input.FindConstDefault(Metasound::Frontend::DefaultPageID))
		{
			Object->SetObjectField(TEXT("default"), MetaSoundToolUtils::LiteralToJsonObject(*DefaultLiteral));
		}

		return Object;
	}

	TSharedPtr<FJsonObject> SerializeClassOutput(const FMetasoundFrontendClassOutput& Output)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Output.Name.ToString());
		Object->SetStringField(TEXT("type"), Output.TypeName.ToString());
		Object->SetStringField(TEXT("vertex_id"), Output.VertexID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("node_id"), Output.NodeID.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeVertex(const FMetasoundFrontendVertex& Vertex)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Vertex.Name.ToString());
		Object->SetStringField(TEXT("type"), Vertex.TypeName.ToString());
		Object->SetStringField(TEXT("vertex_id"), Vertex.VertexID.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeNode(const FMetasoundFrontendNode& Node)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("node_id"), Node.GetID().ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("name"), Node.Name.ToString());
		Object->SetStringField(TEXT("class_id"), Node.ClassID.ToString(EGuidFormats::DigitsWithHyphens));

		TArray<TSharedPtr<FJsonValue>> Inputs;
		for (const FMetasoundFrontendVertex& Input : Node.Interface.Inputs)
		{
			Inputs.Add(MakeShareable(new FJsonValueObject(SerializeVertex(Input))));
		}
		Object->SetArrayField(TEXT("inputs"), Inputs);
		Object->SetNumberField(TEXT("input_count"), Inputs.Num());

		TArray<TSharedPtr<FJsonValue>> Outputs;
		for (const FMetasoundFrontendVertex& Output : Node.Interface.Outputs)
		{
			Outputs.Add(MakeShareable(new FJsonValueObject(SerializeVertex(Output))));
		}
		Object->SetArrayField(TEXT("outputs"), Outputs);
		Object->SetNumberField(TEXT("output_count"), Outputs.Num());
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeEdge(const FMetasoundFrontendEdge& Edge)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("from_node_id"), Edge.FromNodeID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("from_vertex_id"), Edge.FromVertexID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("to_node_id"), Edge.ToNodeID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("to_vertex_id"), Edge.ToVertexID.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}
}

namespace MetaSoundToolUtils
{
	FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	FString BuilderResultToString(EMetaSoundBuilderResult Result)
	{
		const UEnum* Enum = StaticEnum<EMetaSoundBuilderResult>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Result)) : FString::FromInt(static_cast<int32>(Result));
	}

	FString OutputFormatToString(EMetaSoundOutputAudioFormat Format)
	{
		switch (Format)
		{
		case EMetaSoundOutputAudioFormat::Mono:
			return TEXT("mono");
		case EMetaSoundOutputAudioFormat::Stereo:
			return TEXT("stereo");
		case EMetaSoundOutputAudioFormat::Quad:
			return TEXT("quad");
		case EMetaSoundOutputAudioFormat::FiveDotOne:
			return TEXT("5.1");
		case EMetaSoundOutputAudioFormat::SevenDotOne:
			return TEXT("7.1");
		default:
			return TEXT("unknown");
		}
	}

	bool TryResolveOutputFormat(const FString& Name, EMetaSoundOutputAudioFormat& OutFormat, FString& OutError)
	{
		const FString Lower = NormalizeTypeName(Name.IsEmpty() ? TEXT("mono") : Name);
		if (Lower == TEXT("mono"))
		{
			OutFormat = EMetaSoundOutputAudioFormat::Mono;
			return true;
		}
		if (Lower == TEXT("stereo"))
		{
			OutFormat = EMetaSoundOutputAudioFormat::Stereo;
			return true;
		}
		if (Lower == TEXT("quad"))
		{
			OutFormat = EMetaSoundOutputAudioFormat::Quad;
			return true;
		}
		if (Lower == TEXT("5.1") || Lower == TEXT("fivedotone") || Lower == TEXT("five_dot_one"))
		{
			OutFormat = EMetaSoundOutputAudioFormat::FiveDotOne;
			return true;
		}
		if (Lower == TEXT("7.1") || Lower == TEXT("sevendotone") || Lower == TEXT("seven_dot_one"))
		{
			OutFormat = EMetaSoundOutputAudioFormat::SevenDotOne;
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported MetaSound output format '%s'"), *Name);
		return false;
	}

	bool TryResolveDataType(const FString& TypeName, FName& OutDataType, FString& OutError)
	{
		const FString Lower = NormalizeTypeName(TypeName);
		if (Lower == TEXT("bool") || Lower == TEXT("boolean"))
		{
			OutDataType = TEXT("Bool");
			return true;
		}
		if (Lower == TEXT("int") || Lower == TEXT("int32") || Lower == TEXT("integer"))
		{
			OutDataType = TEXT("Int32");
			return true;
		}
		if (Lower == TEXT("float") || Lower == TEXT("number") || Lower == TEXT("scalar"))
		{
			OutDataType = TEXT("Float");
			return true;
		}
		if (Lower == TEXT("string") || Lower == TEXT("name"))
		{
			OutDataType = TEXT("String");
			return true;
		}
		if (Lower == TEXT("trigger"))
		{
			OutDataType = TEXT("Trigger");
			return true;
		}
		if (Lower == TEXT("audio"))
		{
			OutDataType = TEXT("Audio");
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported MetaSound v1 data type '%s'"), *TypeName);
		return false;
	}

	bool TryReadLiteral(const FString& TypeName, const TSharedPtr<FJsonValue>& Value, FMetasoundFrontendLiteral& OutLiteral, FString& OutError)
	{
		const FString Lower = NormalizeTypeName(TypeName);
		OutLiteral = FMetasoundFrontendLiteral();

		if (Lower == TEXT("bool") || Lower == TEXT("boolean"))
		{
			bool BoolValue = false;
			if (Value.IsValid() && !TryReadBool(Value, BoolValue))
			{
				OutError = TEXT("Expected boolean MetaSound literal value");
				return false;
			}
			OutLiteral.Set(BoolValue);
			return true;
		}
		if (Lower == TEXT("int") || Lower == TEXT("int32") || Lower == TEXT("integer"))
		{
			double NumberValue = 0.0;
			if (Value.IsValid() && !TryReadNumber(Value, NumberValue))
			{
				OutError = TEXT("Expected integer MetaSound literal value");
				return false;
			}
			OutLiteral.Set(static_cast<int32>(NumberValue));
			return true;
		}
		if (Lower == TEXT("float") || Lower == TEXT("number") || Lower == TEXT("scalar"))
		{
			double NumberValue = 0.0;
			if (Value.IsValid() && !TryReadNumber(Value, NumberValue))
			{
				OutError = TEXT("Expected float MetaSound literal value");
				return false;
			}
			OutLiteral.Set(static_cast<float>(NumberValue));
			return true;
		}
		if (Lower == TEXT("string") || Lower == TEXT("name"))
		{
			FString StringValue;
			if (Value.IsValid() && !Value->TryGetString(StringValue))
			{
				OutError = TEXT("Expected string MetaSound literal value");
				return false;
			}
			OutLiteral.Set(StringValue);
			return true;
		}
		if (Lower == TEXT("trigger") || Lower == TEXT("audio"))
		{
			OutLiteral.Set(FMetasoundFrontendLiteral::FDefault());
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported MetaSound v1 literal type '%s'"), *TypeName);
		return false;
	}

	TSharedPtr<FJsonValue> LiteralToJsonValue(const FMetasoundFrontendLiteral& Literal)
	{
		switch (Literal.GetType())
		{
		case EMetasoundFrontendLiteralType::Boolean:
		{
			bool Value = false;
			return MakeShareable(new FJsonValueBoolean(Literal.TryGet(Value) ? Value : false));
		}
		case EMetasoundFrontendLiteralType::Integer:
		{
			int32 Value = 0;
			return MakeShareable(new FJsonValueNumber(Literal.TryGet(Value) ? Value : 0));
		}
		case EMetasoundFrontendLiteralType::Float:
		{
			float Value = 0.0f;
			return MakeShareable(new FJsonValueNumber(Literal.TryGet(Value) ? Value : 0.0f));
		}
		case EMetasoundFrontendLiteralType::String:
		{
			FString Value;
			return MakeShareable(new FJsonValueString(Literal.TryGet(Value) ? Value : FString()));
		}
		case EMetasoundFrontendLiteralType::UObject:
		{
			UObject* Value = nullptr;
			Literal.TryGet(Value);
			return MakeShareable(new FJsonValueString(ObjectPath(Value)));
		}
		case EMetasoundFrontendLiteralType::BooleanArray:
		{
			TArray<bool> Values;
			Literal.TryGet(Values);
			return MakeShareable(new FJsonValueArray(BoolArrayToJson(Values)));
		}
		case EMetasoundFrontendLiteralType::IntegerArray:
		{
			TArray<int32> Values;
			Literal.TryGet(Values);
			return MakeShareable(new FJsonValueArray(IntArrayToJson(Values)));
		}
		case EMetasoundFrontendLiteralType::FloatArray:
		{
			TArray<float> Values;
			Literal.TryGet(Values);
			return MakeShareable(new FJsonValueArray(FloatArrayToJson(Values)));
		}
		case EMetasoundFrontendLiteralType::StringArray:
		{
			TArray<FString> Values;
			Literal.TryGet(Values);
			return MakeShareable(new FJsonValueArray(StringArrayToJson(Values)));
		}
		case EMetasoundFrontendLiteralType::UObjectArray:
		{
			TArray<UObject*> Values;
			Literal.TryGet(Values);
			return MakeShareable(new FJsonValueArray(ObjectArrayToJson(Values)));
		}
		default:
			return MakeShareable(new FJsonValueNull());
		}
	}

	TSharedPtr<FJsonObject> LiteralToJsonObject(const FMetasoundFrontendLiteral& Literal)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		const UEnum* Enum = StaticEnum<EMetasoundFrontendLiteralType>();
		Object->SetStringField(TEXT("literal_type"), Enum ? Enum->GetNameStringByValue(static_cast<int64>(Literal.GetType())) : FString::FromInt(static_cast<int32>(Literal.GetType())));
		Object->SetBoolField(TEXT("valid"), Literal.IsValid());
		Object->SetBoolField(TEXT("is_array"), Literal.IsArray());
		Object->SetNumberField(TEXT("array_num"), Literal.GetArrayNum());
		Object->SetField(TEXT("value"), LiteralToJsonValue(Literal));
		return Object;
	}

	bool TryLoadSource(const FString& AssetPath, UMetaSoundSource*& OutSource, FString& OutError)
	{
		OutSource = FMcpAssetModifier::LoadAssetByPath<UMetaSoundSource>(AssetPath, OutError);
		return OutSource != nullptr;
	}

	bool TryBeginBuilding(UMetaSoundSource* Source, UMetaSoundBuilderBase*& OutBuilder, FString& OutError)
	{
		OutBuilder = nullptr;
		if (!Source)
		{
			OutError = TEXT("MetaSound Source is required");
			return false;
		}

		if (FModuleManager::Get().ModuleExists(TEXT("MetasoundEditor")) && !FModuleManager::Get().IsModuleLoaded(TEXT("MetasoundEditor")))
		{
			FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("MetasoundEditor"));
		}

		TScriptInterface<IMetaSoundDocumentInterface> DocumentInterface;
		DocumentInterface.SetObject(Source);
		DocumentInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(Source));
		if (!DocumentInterface.GetInterface())
		{
			OutError = TEXT("Asset does not implement IMetaSoundDocumentInterface");
			return false;
		}

		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		OutBuilder = UMetaSoundEditorSubsystem::GetChecked().FindOrBeginBuilding(DocumentInterface, Result);
		if (!OutBuilder || Result != EMetaSoundBuilderResult::Succeeded)
		{
			OutError = FString::Printf(TEXT("Failed to begin building MetaSound Source: %s"), *BuilderResultToString(Result));
			return false;
		}
		return true;
	}

	bool BuildExistingSource(UMetaSoundSource* Source, UMetaSoundBuilderBase* Builder, FString& OutError)
	{
		if (!Source || !Builder)
		{
			OutError = TEXT("MetaSound Source and builder are required");
			return false;
		}

		TScriptInterface<IMetaSoundDocumentInterface> DocumentInterface;
		DocumentInterface.SetObject(Source);
		DocumentInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(Source));
		if (!DocumentInterface.GetInterface())
		{
			OutError = TEXT("Asset does not implement IMetaSoundDocumentInterface");
			return false;
		}

		Builder->InitNodeLocations();
		Builder->BuildAndOverwriteMetaSound(DocumentInterface, false);
		Builder->ConformObjectToDocument();
		Source->PostEditChange();
		FMcpAssetModifier::MarkPackageDirty(Source);
		OutError.Reset();
		return true;
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

	TSharedPtr<FJsonObject> SerializeSourceSummary(UMetaSoundSource* Source, bool bIncludeGraph)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Source)
		{
			Object->SetBoolField(TEXT("valid"), false);
			return Object;
		}

		const FMetasoundFrontendDocument& Document = Source->GetConstDocument();
		const FMetasoundFrontendClassInterface& Interface = Document.RootGraph.GetDefaultInterface();
		const FMetasoundFrontendGraph& DefaultGraph = Document.RootGraph.GetConstDefaultGraph();

		Object->SetBoolField(TEXT("valid"), true);
		Object->SetStringField(TEXT("asset_path"), ObjectPath(Source));
		Object->SetStringField(TEXT("asset_class"), Source->GetClass()->GetName());
		Object->SetStringField(TEXT("name"), Source->GetName());
		Object->SetStringField(TEXT("output_format"), OutputFormatToString(Source->OutputFormat));
		Object->SetNumberField(TEXT("num_channels"), Source->NumChannels);
		Object->SetNumberField(TEXT("duration"), Source->GetDuration());
		Object->SetNumberField(TEXT("sample_rate"), Source->GetSampleRateForCurrentPlatform());
		Object->SetArrayField(TEXT("interfaces"), VersionSetToJson(Document.Interfaces));
		Object->SetNumberField(TEXT("interface_count"), Document.Interfaces.Num());

		TArray<TSharedPtr<FJsonValue>> Inputs;
		for (const FMetasoundFrontendClassInput& Input : Interface.Inputs)
		{
			Inputs.Add(MakeShareable(new FJsonValueObject(SerializeClassInput(Input))));
		}
		Object->SetArrayField(TEXT("inputs"), Inputs);
		Object->SetNumberField(TEXT("input_count"), Inputs.Num());

		TArray<TSharedPtr<FJsonValue>> Outputs;
		for (const FMetasoundFrontendClassOutput& Output : Interface.Outputs)
		{
			Outputs.Add(MakeShareable(new FJsonValueObject(SerializeClassOutput(Output))));
		}
		Object->SetArrayField(TEXT("outputs"), Outputs);
		Object->SetNumberField(TEXT("output_count"), Outputs.Num());

		Object->SetNumberField(TEXT("node_count"), DefaultGraph.Nodes.Num());
		Object->SetNumberField(TEXT("edge_count"), DefaultGraph.Edges.Num());
		Object->SetNumberField(TEXT("variable_count"), DefaultGraph.Variables.Num());

		if (bIncludeGraph)
		{
			TArray<TSharedPtr<FJsonValue>> Nodes;
			for (const FMetasoundFrontendNode& Node : DefaultGraph.Nodes)
			{
				Nodes.Add(MakeShareable(new FJsonValueObject(SerializeNode(Node))));
			}
			Object->SetArrayField(TEXT("nodes"), Nodes);

			TArray<TSharedPtr<FJsonValue>> Edges;
			for (const FMetasoundFrontendEdge& Edge : DefaultGraph.Edges)
			{
				Edges.Add(MakeShareable(new FJsonValueObject(SerializeEdge(Edge))));
			}
			Object->SetArrayField(TEXT("edges"), Edges);
		}

		return Object;
	}

	TSharedPtr<FJsonObject> SerializeNodeHandle(const FMetaSoundNodeHandle& Handle)
	{
		TSharedPtr<FJsonObject> Object = GuidObject(Handle.NodeID);
		Object->SetStringField(TEXT("node_id"), Handle.NodeID.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeInputHandle(const FMetaSoundBuilderNodeInputHandle& Handle)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("node_id"), Handle.NodeID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("vertex_id"), Handle.VertexID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetBoolField(TEXT("valid"), Handle.IsSet());
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeOutputHandle(const FMetaSoundBuilderNodeOutputHandle& Handle)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("node_id"), Handle.NodeID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("vertex_id"), Handle.VertexID.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetBoolField(TEXT("valid"), Handle.IsSet());
		return Object;
	}
}
