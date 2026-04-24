// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/QueryReplicationSummaryTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"

#include "Engine/Blueprint.h"

namespace
{
	TSharedPtr<FJsonValue> SerializePropertyPath(UObject* TargetObject, const FString& PropertyPath)
	{
		FProperty* Property = nullptr;
		void* Container = nullptr;
		FString FindError;
		if (!FMcpAssetModifier::FindPropertyByPath(TargetObject, PropertyPath, Property, Container, FindError))
		{
			return MakeShareable(new FJsonValueNull());
		}

		return FMcpPropertySerializer::SerializePropertyValue(Property, Container, TargetObject, 0, 1);
	}

	TSharedPtr<FJsonObject> BuildVariableReplicationObject(const FBPVariableDescription& Variable)
	{
		TSharedPtr<FJsonObject> VariableObject = MakeShareable(new FJsonObject);
		VariableObject->SetStringField(TEXT("name"), Variable.VarName.ToString());
		VariableObject->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
		if (Variable.VarType.PinSubCategoryObject.IsValid())
		{
			VariableObject->SetStringField(TEXT("sub_type"), Variable.VarType.PinSubCategoryObject->GetPathName());
		}

		if (Variable.RepNotifyFunc != NAME_None)
		{
			VariableObject->SetStringField(TEXT("replication"), TEXT("repnotify"));
			VariableObject->SetStringField(TEXT("replicated_using"), Variable.RepNotifyFunc.ToString());
		}
		else if (Variable.PropertyFlags & CPF_Net)
		{
			VariableObject->SetStringField(TEXT("replication"), TEXT("replicated"));
		}
		else
		{
			VariableObject->SetStringField(TEXT("replication"), TEXT("none"));
		}

		return VariableObject;
	}
}

FString UQueryReplicationSummaryTool::GetToolDescription() const
{
	return TEXT("Query Blueprint replication settings for class defaults and authored member variables.");
}

TMap<FString, FMcpSchemaProperty> UQueryReplicationSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	return Schema;
}

TArray<FString> UQueryReplicationSummaryTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UQueryReplicationSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	FString LoadError;
	UBlueprint* Blueprint = FMcpAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TSharedPtr<FJsonObject> ClassSettings = MakeShareable(new FJsonObject);
	ClassSettings->SetBoolField(TEXT("applicable"), false);

	UObject* DefaultObject = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	if (DefaultObject && DefaultObject->IsA<AActor>())
	{
		ClassSettings->SetBoolField(TEXT("applicable"), true);
		ClassSettings->SetField(TEXT("replicates"), SerializePropertyPath(DefaultObject, TEXT("bReplicates")));
		ClassSettings->SetField(TEXT("replicate_movement"), SerializePropertyPath(DefaultObject, TEXT("bReplicateMovement")));
		ClassSettings->SetField(TEXT("always_relevant"), SerializePropertyPath(DefaultObject, TEXT("bAlwaysRelevant")));
		ClassSettings->SetField(TEXT("only_relevant_to_owner"), SerializePropertyPath(DefaultObject, TEXT("bOnlyRelevantToOwner")));
		ClassSettings->SetField(TEXT("use_owner_relevancy"), SerializePropertyPath(DefaultObject, TEXT("bNetUseOwnerRelevancy")));
		ClassSettings->SetField(TEXT("net_cull_distance_squared"), SerializePropertyPath(DefaultObject, TEXT("NetCullDistanceSquared")));
		ClassSettings->SetField(TEXT("net_update_frequency"), SerializePropertyPath(DefaultObject, TEXT("NetUpdateFrequency")));
		ClassSettings->SetField(TEXT("min_net_update_frequency"), SerializePropertyPath(DefaultObject, TEXT("MinNetUpdateFrequency")));
		ClassSettings->SetField(TEXT("dormancy"), SerializePropertyPath(DefaultObject, TEXT("NetDormancy")));
	}
	else
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("Blueprint does not currently expose an actor default object for class replication settings"))));
	}

	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		VariablesArray.Add(MakeShareable(new FJsonValueObject(BuildVariableReplicationObject(Variable))));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Response->SetObjectField(TEXT("class_settings"), ClassSettings);
	Response->SetArrayField(TEXT("variables"), VariablesArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	return FMcpToolResult::StructuredJson(Response);
}
