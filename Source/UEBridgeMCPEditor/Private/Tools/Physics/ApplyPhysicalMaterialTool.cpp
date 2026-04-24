// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Physics/ApplyPhysicalMaterialTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ScopedTransaction.h"

FString UApplyPhysicalMaterialTool::GetToolDescription() const
{
	return TEXT("Apply a PhysicalMaterial asset as the override physical material on an actor PrimitiveComponent.");
}

TMap<FString, FMcpSchemaProperty> UApplyPhysicalMaterialTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("physical_material_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PhysicalMaterial asset path"), true));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PrimitiveComponent name; defaults to root or first primitive component")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without mutating")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited map when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on failure")));
	return Schema;
}

FMcpToolResult UApplyPhysicalMaterialTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString PhysicalMaterialPath = GetStringArgOrDefault(Arguments, TEXT("physical_material_path"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	FString LoadError;
	UPhysicalMaterial* PhysicalMaterial = nullptr;
	if (!PhysicsToolUtils::TryLoadPhysicalMaterial(PhysicalMaterialPath, PhysicalMaterial, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

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

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Result = PhysicsToolUtils::SerializePrimitiveComponent(Component);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Result->SetStringField(TEXT("physical_material_path"), PhysicalMaterialPath);
		Result->SetBoolField(TEXT("would_change"), true);
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Physical material dry run complete"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Apply Physical Material")));
	Actor->Modify();
	Component->Modify();
	Component->SetPhysMaterialOverride(PhysicalMaterial);
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
	Result->SetStringField(TEXT("physical_material_path"), PhysicalMaterialPath);
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Physical material applied"));
}
