// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Physics/CreatePhysicsConstraintTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "ScopedTransaction.h"

namespace
{
	void AddConstraintSettingSchemas(TMap<FString, FMcpSchemaProperty>& Schema)
	{
		Schema.Add(TEXT("disable_collision"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Disable collision between constrained bodies")));
		Schema.Add(TEXT("projection_enabled"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Enable constraint projection")));
		Schema.Add(TEXT("linear_breakable"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Enable linear break threshold")));
		Schema.Add(TEXT("linear_break_threshold"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Linear break threshold")));
		Schema.Add(TEXT("angular_breakable"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Enable angular break threshold")));
		Schema.Add(TEXT("angular_break_threshold"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Angular break threshold")));
		Schema.Add(TEXT("linear_limit"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Linear limit object: motion/x_motion/y_motion/z_motion and limit_cm")));
		Schema.Add(TEXT("angular_limit"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Angular limit object: swing1_motion/swing2_motion/twist_motion plus degrees")));
	}
}

FString UCreatePhysicsConstraintTool::GetToolDescription() const
{
	return TEXT("Create a PhysicsConstraintComponent on an actor and bind it to two PrimitiveComponents with optional v1 limit settings.");
}

TMap<FString, FMcpSchemaProperty> UCreatePhysicsConstraintTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor that will own the new constraint component")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Owner actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("New PhysicsConstraintComponent name")));
	Schema.Add(TEXT("component1_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PrimitiveComponent name on owner actor; defaults to root/first primitive")));
	Schema.Add(TEXT("component2_actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional second actor label or name; defaults to owner actor")));
	Schema.Add(TEXT("component2_actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional second actor handle")));
	Schema.Add(TEXT("component2_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PrimitiveComponent name on second actor; defaults to root/first primitive")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without creating the component")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited map when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on failure")));
	AddConstraintSettingSchemas(Schema);
	return Schema;
}

FMcpToolResult UCreatePhysicsConstraintTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ConstraintName = GetStringArgOrDefault(Arguments, TEXT("component_name"), TEXT("PhysicsConstraintMCPComponent"));
	const FString Component1Name = GetStringArgOrDefault(Arguments, TEXT("component1_name"));
	const FString Component2Name = GetStringArgOrDefault(Arguments, TEXT("component2_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	UWorld* World = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	AActor* OwnerActor = LevelActorToolUtils::ResolveActorReference(
		Arguments,
		WorldType,
		TEXT("actor_name"),
		TEXT("actor_handle"),
		Context,
		World,
		ErrorCode,
		ErrorMessage,
		ErrorDetails,
		true);
	if (!OwnerActor)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	if (!ConstraintName.IsEmpty() && FMcpAssetModifier::FindComponentByName(OwnerActor, ConstraintName))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_ALREADY_EXISTS"), FString::Printf(TEXT("Component '%s' already exists on actor '%s'"), *ConstraintName, *OwnerActor->GetActorNameOrLabel()));
	}

	AActor* SecondActor = OwnerActor;
	if (Arguments->HasField(TEXT("component2_actor_name")) || Arguments->HasField(TEXT("component2_actor_handle")))
	{
		SecondActor = LevelActorToolUtils::ResolveActorReference(
			Arguments,
			WorldType,
			TEXT("component2_actor_name"),
			TEXT("component2_actor_handle"),
			Context,
			World,
			ErrorCode,
			ErrorMessage,
			ErrorDetails,
			true);
		if (!SecondActor)
		{
			return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
		}
	}

	UPrimitiveComponent* Component1 = PhysicsToolUtils::ResolvePrimitiveComponent(OwnerActor, Component1Name, ErrorCode, ErrorMessage);
	if (!Component1)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}
	UPrimitiveComponent* Component2 = PhysicsToolUtils::ResolvePrimitiveComponent(SecondActor, Component2Name, ErrorCode, ErrorMessage);
	if (!Component2)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetBoolField(TEXT("would_create_component"), true);
		Result->SetStringField(TEXT("actor_name"), OwnerActor->GetActorNameOrLabel());
		Result->SetStringField(TEXT("component_name"), ConstraintName);
		Result->SetObjectField(TEXT("component1"), PhysicsToolUtils::SerializePrimitiveComponent(Component1));
		Result->SetObjectField(TEXT("component2"), PhysicsToolUtils::SerializePrimitiveComponent(Component2));
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Physics constraint dry run complete"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Physics Constraint")));
	OwnerActor->Modify();
	if (SecondActor != OwnerActor)
	{
		SecondActor->Modify();
	}

	UPhysicsConstraintComponent* Constraint = PhysicsToolUtils::CreateConstraintComponent(OwnerActor, ConstraintName);
	if (!Constraint)
	{
		if (Transaction.IsValid() && bRollbackOnError)
		{
			Transaction->Cancel();
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_CREATE_FAILED"), TEXT("Failed to create PhysicsConstraintComponent"));
	}

	Constraint->Modify();
	Constraint->SetWorldLocation((Component1->GetComponentLocation() + Component2->GetComponentLocation()) * 0.5f);
	Constraint->SetConstrainedComponents(Component1, NAME_None, Component2, NAME_None);
	if (!PhysicsToolUtils::ApplyConstraintSettings(Constraint, Arguments, ErrorMessage))
	{
		if (Transaction.IsValid() && bRollbackOnError)
		{
			Transaction->Cancel();
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ErrorMessage);
	}

	PhysicsToolUtils::FinalizeActorPhysicsEdit(OwnerActor);
	PhysicsToolUtils::FinalizeActorPhysicsEdit(SecondActor);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	FString SaveErrorCode;
	FString SaveErrorMessage;
	if (!LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, Warnings, ModifiedAssets, SaveErrorCode, SaveErrorMessage) && bRollbackOnError)
	{
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
		}
		return FMcpToolResult::StructuredError(SaveErrorCode, SaveErrorMessage);
	}

	TSharedPtr<FJsonObject> Result = PhysicsToolUtils::SerializeConstraintComponent(Constraint);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("dry_run"), false);
	Result->SetStringField(TEXT("actor_name"), OwnerActor->GetActorNameOrLabel());
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Physics constraint created"));
}
