// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Physics/EditPhysicsSimulationTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

FString UEditPhysicsSimulationTool::GetToolDescription() const
{
	return TEXT("Edit physics simulation settings on an actor PrimitiveComponent: simulate physics, gravity, mass, mass scale, and damping.");
}

TMap<FString, FMcpSchemaProperty> UEditPhysicsSimulationTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PrimitiveComponent name; defaults to root or first primitive component")));
	Schema.Add(TEXT("simulate_physics"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Set simulate physics")));
	Schema.Add(TEXT("gravity_enabled"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Enable or disable gravity")));
	Schema.Add(TEXT("mass_kg"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Override mass in kilograms")));
	Schema.Add(TEXT("mass_scale"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Mass scale")));
	Schema.Add(TEXT("linear_damping"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Linear damping")));
	Schema.Add(TEXT("angular_damping"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Angular damping")));
	Schema.Add(TEXT("mobility"), FMcpSchemaProperty::MakeEnum(TEXT("Optional scene component mobility"), { TEXT("Static"), TEXT("Stationary"), TEXT("Movable") }));
	Schema.Add(TEXT("make_movable_if_needed"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("When simulate_physics=true, set scene component mobility to Movable if needed")));
	Schema.Add(TEXT("wake"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Wake rigid body after edits")));
	Schema.Add(TEXT("sleep"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Put rigid body to sleep after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without mutating")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited map when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on failure")));
	return Schema;
}

FMcpToolResult UEditPhysicsSimulationTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bMakeMovableIfNeeded = GetBoolArgOrDefault(Arguments, TEXT("make_movable_if_needed"), true);

	UWorld* World = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	AActor* Actor = LevelActorToolUtils::ResolveActorReference(
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
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	UPrimitiveComponent* Component = PhysicsToolUtils::ResolvePrimitiveComponent(Actor, ComponentName, ErrorCode, ErrorMessage);
	if (!Component)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	EComponentMobility::Type RequestedMobility = EComponentMobility::Movable;
	FString MobilityName;
	if (Arguments->TryGetStringField(TEXT("mobility"), MobilityName) &&
		!LevelActorToolUtils::TryParseMobility(MobilityName, RequestedMobility))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported mobility '%s'"), *MobilityName));
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Result = PhysicsToolUtils::SerializePrimitiveComponent(Component);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Result->SetBoolField(TEXT("would_change"), true);
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Physics simulation dry run complete"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Physics Simulation")));
	Actor->Modify();
	Component->Modify();

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		if (Arguments->HasField(TEXT("mobility")))
		{
			SceneComponent->SetMobility(RequestedMobility);
		}
		else if (GetBoolArgOrDefault(Arguments, TEXT("simulate_physics"), false) && bMakeMovableIfNeeded && SceneComponent->Mobility != EComponentMobility::Movable)
		{
			SceneComponent->SetMobility(EComponentMobility::Movable);
		}
	}

	bool bBoolValue = false;
	if (Arguments->TryGetBoolField(TEXT("simulate_physics"), bBoolValue))
	{
		Component->SetSimulatePhysics(bBoolValue);
	}
	if (Arguments->TryGetBoolField(TEXT("gravity_enabled"), bBoolValue))
	{
		Component->SetEnableGravity(bBoolValue);
	}

	double NumberValue = 0.0;
	if (Arguments->TryGetNumberField(TEXT("mass_kg"), NumberValue))
	{
		Component->SetMassOverrideInKg(NAME_None, static_cast<float>(NumberValue), true);
	}
	if (Arguments->TryGetNumberField(TEXT("mass_scale"), NumberValue))
	{
		Component->SetMassScale(NAME_None, static_cast<float>(NumberValue));
	}
	if (Arguments->TryGetNumberField(TEXT("linear_damping"), NumberValue))
	{
		Component->SetLinearDamping(static_cast<float>(NumberValue));
	}
	if (Arguments->TryGetNumberField(TEXT("angular_damping"), NumberValue))
	{
		Component->SetAngularDamping(static_cast<float>(NumberValue));
	}
	if (GetBoolArgOrDefault(Arguments, TEXT("wake"), false))
	{
		Component->WakeRigidBody();
	}
	if (GetBoolArgOrDefault(Arguments, TEXT("sleep"), false))
	{
		Component->PutRigidBodyToSleep();
	}

	PhysicsToolUtils::FinalizeActorPhysicsEdit(Actor);

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

	TSharedPtr<FJsonObject> Result = PhysicsToolUtils::SerializePrimitiveComponent(Component);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("dry_run"), false);
	Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Physics simulation updated"));
}
