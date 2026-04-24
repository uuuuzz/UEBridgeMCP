// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Analysis/QueryClassMemberSummaryTool.h"

#include "Tools/Analysis/EngineApiToolUtils.h"
#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"
#include "UObject/UnrealType.h"

FString UQueryClassMemberSummaryTool::GetToolDescription() const
{
	return TEXT("Return a structured local-reflection summary of a class, including functions, properties, inheritance, flags, metadata, and optional member filtering.");
}

TMap<FString, FMcpSchemaProperty> UQueryClassMemberSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("class_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Class name or path, e.g. Actor, AActor, /Script/Engine.Actor."), true));
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional member name/type query.")));
	Schema.Add(TEXT("member_types"), FMcpSchemaProperty::MakeArray(TEXT("Optional member types: function, property. Defaults to both."), TEXT("string")));
	Schema.Add(TEXT("include_inherited"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include inherited members. Default: true.")));
	Schema.Add(TEXT("include_metadata"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include selected UHT metadata. Default: true.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum members of each type to return. Default: 200, max: 1000.")));
	return Schema;
}

FMcpToolResult UQueryClassMemberSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString ClassName = GetStringArgOrDefault(Arguments, TEXT("class_name"));
	FString ResolveError;
	UClass* Class = EngineApiToolUtils::ResolveClass(ClassName, ResolveError);
	if (!Class)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), ResolveError);
	}

	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	const TSet<FString> MemberTypes = EngineApiToolUtils::ReadLowercaseStringSet(Arguments, TEXT("member_types"));
	const bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), true);
	const bool bIncludeMetadata = GetBoolArgOrDefault(Arguments, TEXT("include_metadata"), true);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 200), 1, 1000);
	const EFieldIteratorFlags::SuperClassFlags SuperFlag = bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetObjectField(TEXT("class"), EngineApiToolUtils::SerializeClass(Class));
	Result->SetStringField(TEXT("query"), Query);
	Result->SetBoolField(TEXT("include_inherited"), bIncludeInherited);
	Result->SetBoolField(TEXT("include_metadata"), bIncludeMetadata);

	TArray<TSharedPtr<FJsonValue>> Parents;
	for (UClass* Parent = Class->GetSuperClass(); Parent; Parent = Parent->GetSuperClass())
	{
		Parents.Add(MakeShareable(new FJsonValueObject(EngineApiToolUtils::SerializeClass(Parent))));
	}
	Result->SetArrayField(TEXT("parents"), Parents);

	if (EngineApiToolUtils::TypeSetAllows(MemberTypes, TEXT("property")))
	{
		TArray<EngineApiToolUtils::FScoredJson> Properties;
		int32 Scanned = 0;
		for (TFieldIterator<FProperty> It(Class, SuperFlag); It; ++It)
		{
			FProperty* Property = *It;
			++Scanned;
			const double Score = SearchToolUtils::ScoreFields({ Property->GetName(), EngineApiToolUtils::PropertyTypeToString(Property), Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetName() : FString() }, Query);
			if (!Query.IsEmpty() && Score <= 0.0)
			{
				continue;
			}
			Properties.Add({ Score, EngineApiToolUtils::SerializeProperty(Property, Cast<UClass>(Property->GetOwnerStruct()), bIncludeMetadata, Score) });
		}
		TArray<TSharedPtr<FJsonValue>> PropertyArray;
		EngineApiToolUtils::SortAndTrim(Properties, Limit, PropertyArray);
		Result->SetArrayField(TEXT("properties"), PropertyArray);
		Result->SetNumberField(TEXT("property_count"), PropertyArray.Num());
		Result->SetNumberField(TEXT("properties_scanned"), Scanned);
		Result->SetBoolField(TEXT("properties_truncated"), Properties.Num() > PropertyArray.Num());
	}

	if (EngineApiToolUtils::TypeSetAllows(MemberTypes, TEXT("function")))
	{
		TArray<EngineApiToolUtils::FScoredJson> Functions;
		int32 Scanned = 0;
		for (TFieldIterator<UFunction> It(Class, SuperFlag); It; ++It)
		{
			UFunction* Function = *It;
			++Scanned;
			const double Score = SearchToolUtils::ScoreFields({ Function->GetName(), EngineApiToolUtils::FunctionFlagsToString(Function), Function->GetOuterUClass() ? Function->GetOuterUClass()->GetName() : FString() }, Query);
			if (!Query.IsEmpty() && Score <= 0.0)
			{
				continue;
			}
			Functions.Add({ Score, EngineApiToolUtils::SerializeFunction(Function, Function->GetOuterUClass(), bIncludeMetadata, Score) });
		}
		TArray<TSharedPtr<FJsonValue>> FunctionArray;
		EngineApiToolUtils::SortAndTrim(Functions, Limit, FunctionArray);
		Result->SetArrayField(TEXT("functions"), FunctionArray);
		Result->SetNumberField(TEXT("function_count"), FunctionArray.Num());
		Result->SetNumberField(TEXT("functions_scanned"), Scanned);
		Result->SetBoolField(TEXT("functions_truncated"), Functions.Num() > FunctionArray.Num());
	}

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Class member summary ready"));
}
