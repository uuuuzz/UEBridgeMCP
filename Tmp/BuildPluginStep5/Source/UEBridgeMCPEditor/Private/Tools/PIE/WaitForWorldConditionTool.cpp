// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/WaitForWorldConditionTool.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Serialization/JsonSerializer.h"

FString UWaitForWorldConditionTool::GetToolDescription() const
{
	return TEXT("Check world conditions (actor existence, property values, tags, etc.) in editor or PIE world. "
		"MVP mode is 'check' (instant evaluation). 'wait' mode is reserved for future async implementation.");
}

TMap<FString, FMcpSchemaProperty> UWaitForWorldConditionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("World to check: 'pie', 'editor', or 'auto'"),
		{TEXT("pie"), TEXT("editor"), TEXT("auto")}));

	Schema.Add(TEXT("mode"), FMcpSchemaProperty::MakeEnum(
		TEXT("Evaluation mode: 'check' (instant) or 'wait' (blocking, not yet implemented)"),
		{TEXT("check"), TEXT("wait")}));

	TSharedPtr<FMcpSchemaProperty> ExpectedValueSchema = MakeShared<FMcpSchemaProperty>();
	ExpectedValueSchema->Description = TEXT("Expected value for property comparison conditions. Accepts any JSON value type.");
	TSharedPtr<FJsonObject> ExpectedValueRawSchema = MakeShareable(new FJsonObject);
	ExpectedValueRawSchema->SetStringField(TEXT("description"), ExpectedValueSchema->Description);
	ExpectedValueSchema->RawSchema = ExpectedValueRawSchema;

	TSharedPtr<FMcpSchemaProperty> ConditionItemSchema = MakeShared<FMcpSchemaProperty>();
	ConditionItemSchema->Type = TEXT("object");
	ConditionItemSchema->Description = TEXT("Single condition descriptor. Fields required vary by type.");
	ConditionItemSchema->NestedRequired = {TEXT("type")};
	ConditionItemSchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Condition kind"),
		{TEXT("actor_exists"), TEXT("actor_not_exists"), TEXT("property_equals"), TEXT("property_not_equals"), TEXT("component_exists"), TEXT("tag_present"), TEXT("distance_less_than"), TEXT("log_contains")},
		true)));
	ConditionItemSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Primary actor name for actor/property/tag/component conditions"))));
	ConditionItemSchema->Properties.Add(TEXT("component_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Component name for component_exists"))));
	ConditionItemSchema->Properties.Add(TEXT("property_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Reflected property path for property comparison conditions"))));
	ConditionItemSchema->Properties.Add(TEXT("operator"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Optional comparison operator hint for future expansion"),
		{TEXT("equals"), TEXT("not_equals"), TEXT("less_than"), TEXT("greater_than"), TEXT("contains")})));
	ConditionItemSchema->Properties.Add(TEXT("expected_value"), ExpectedValueSchema);
	ConditionItemSchema->Properties.Add(TEXT("target_actor"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Secondary actor name for distance checks"))));
	ConditionItemSchema->Properties.Add(TEXT("substring"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Substring to search for in log_contains conditions"))));
	ConditionItemSchema->Properties.Add(TEXT("tag_filter"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor tag name for tag_present conditions"))));
	ConditionItemSchema->Properties.Add(TEXT("tolerance"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Numeric tolerance for approximate comparisons"))));

	FMcpSchemaProperty ConditionsSchema;
	ConditionsSchema.Type = TEXT("array");
	ConditionsSchema.Description = TEXT("Array of nested condition descriptors evaluated together; all conditions must be satisfied.");
	ConditionsSchema.bRequired = true;
	ConditionsSchema.Items = ConditionItemSchema;
	Schema.Add(TEXT("conditions"), ConditionsSchema);

	Schema.Add(TEXT("return_snapshot"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Return last observation data")));

	return Schema;
}

TArray<FString> UWaitForWorldConditionTool::GetRequiredParams() const
{
	return {TEXT("conditions")};
}

FMcpToolResult UWaitForWorldConditionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("check"));
	const bool bReturnSnapshot = GetBoolArgOrDefault(Arguments, TEXT("return_snapshot"), true);

	if (Mode == TEXT("wait"))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_NOT_IMPLEMENTED"),
			TEXT("'wait' mode is not yet implemented. Use 'check' mode and poll from client."));
	}

	const TArray<TSharedPtr<FJsonValue>>* ConditionsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("conditions"), ConditionsArray) || !ConditionsArray || ConditionsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'conditions' array is required"));
	}

	// 解析世界
	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);

	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	// 评估所有条件
	bool bAllSatisfied = true;
	TSharedPtr<FJsonObject> LastObservation = MakeShareable(new FJsonObject);

	for (const TSharedPtr<FJsonValue>& CondVal : *ConditionsArray)
	{
		const TSharedPtr<FJsonObject>* CondObj = nullptr;
		if (!CondVal->TryGetObject(CondObj) || !(*CondObj).IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Observation = MakeShareable(new FJsonObject);
		bool bSatisfied = EvaluateCondition(World, *CondObj, Observation);
		if (!bSatisfied)
		{
			bAllSatisfied = false;
		}
		LastObservation = Observation;
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("wait-for-world-condition"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("mode"), TEXT("check"));
	Response->SetBoolField(TEXT("satisfied"), bAllSatisfied);
	Response->SetNumberField(TEXT("checks_performed"), 1);
	Response->SetNumberField(TEXT("elapsed_seconds"), 0.0);
	Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());

	if (bReturnSnapshot && LastObservation.IsValid())
	{
		Response->SetObjectField(TEXT("last_observation"), LastObservation);
	}

	return FMcpToolResult::StructuredJson(Response);
}

bool UWaitForWorldConditionTool::EvaluateCondition(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Condition,
	TSharedPtr<FJsonObject>& OutObservation) const
{
	FString Type;
	if (!Condition->TryGetStringField(TEXT("type"), Type))
	{
		return false;
	}

	OutObservation->SetStringField(TEXT("type"), Type);

	if (Type == TEXT("actor_exists") || Type == TEXT("actor_not_exists"))
	{
		FString ActorName;
		Condition->TryGetStringField(TEXT("actor_name"), ActorName);
		OutObservation->SetStringField(TEXT("actor_name"), ActorName);

		bool bFound = (FMcpAssetModifier::FindActorByName(World, ActorName) != nullptr);

		OutObservation->SetBoolField(TEXT("actor_found"), bFound);
		return (Type == TEXT("actor_exists")) ? bFound : !bFound;
	}
	else if (Type == TEXT("property_equals") || Type == TEXT("property_not_equals"))
	{
		FString ActorName, PropertyPath;
		Condition->TryGetStringField(TEXT("actor_name"), ActorName);
		Condition->TryGetStringField(TEXT("property_path"), PropertyPath);

		AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);

		if (!Actor)
		{
			OutObservation->SetStringField(TEXT("error"), TEXT("Actor not found"));
			return false;
		}

		// 使用反射获取属性值
		FProperty* Prop = nullptr;
		void* Container = nullptr;
		FString PropError;
		if (!FMcpAssetModifier::FindPropertyByPath(Actor, PropertyPath, Prop, Container, PropError))
		{
			OutObservation->SetStringField(TEXT("error"), PropError);
			return false;
		}

		// 简单比较：将属性导出为字符串
		FString PropValue;
		Prop->ExportTextItem_Direct(PropValue, Prop->ContainerPtrToValuePtr<void>(Container), nullptr, nullptr, 0);
		OutObservation->SetStringField(TEXT("actual_value"), PropValue);

		// 获取期望值
		FString ExpectedStr;
		double ExpectedNum = 0;
		if (Condition->TryGetStringField(TEXT("expected_value"), ExpectedStr))
		{
			bool bMatch = (PropValue == ExpectedStr);
			return (Type == TEXT("property_equals")) ? bMatch : !bMatch;
		}
		else if (Condition->TryGetNumberField(TEXT("expected_value"), ExpectedNum))
		{
			double ActualNum = FCString::Atod(*PropValue);
			bool bMatch = FMath::IsNearlyEqual(ActualNum, ExpectedNum, 0.01);
			return (Type == TEXT("property_equals")) ? bMatch : !bMatch;
		}

		return false;
	}
	else if (Type == TEXT("tag_present"))
	{
		FString ActorName, TagName;
		Condition->TryGetStringField(TEXT("actor_name"), ActorName);
		Condition->TryGetStringField(TEXT("tag_filter"), TagName);

		AActor* TagActor = FMcpAssetModifier::FindActorByName(World, ActorName);
		if (TagActor)
		{
			bool bHasTag = TagActor->Tags.Contains(FName(*TagName));
			OutObservation->SetBoolField(TEXT("tag_present"), bHasTag);
			return bHasTag;
		}
		return false;
	}

	OutObservation->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown condition type: '%s'"), *Type));
	return false;
}
