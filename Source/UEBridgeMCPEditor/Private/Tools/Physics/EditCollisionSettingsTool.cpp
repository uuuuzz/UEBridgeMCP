// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Physics/EditCollisionSettingsTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Physics/PhysicsToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FMcpSchemaProperty> MakeResponseSchema()
	{
		TSharedPtr<FMcpSchemaProperty> Schema = MakeShared<FMcpSchemaProperty>();
		Schema->Type = TEXT("object");
		Schema->Description = TEXT("Collision channel response edit");
		Schema->NestedRequired = { TEXT("channel"), TEXT("response") };
		Schema->Properties.Add(TEXT("channel"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Collision channel name"), true)));
		Schema->Properties.Add(TEXT("response"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(TEXT("Response"), { TEXT("Ignore"), TEXT("Overlap"), TEXT("Block") }, true)));
		return Schema;
	}
}

FString UEditCollisionSettingsTool::GetToolDescription() const
{
	return TEXT("Edit collision settings on an actor PrimitiveComponent, including profile, enabled mode, object channel, and channel responses.");
}

TMap<FString, FMcpSchemaProperty> UEditCollisionSettingsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("PrimitiveComponent name; defaults to root or first primitive component")));
	Schema.Add(TEXT("profile_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Collision profile name, e.g. BlockAllDynamic or PhysicsActor")));
	Schema.Add(TEXT("collision_enabled"), FMcpSchemaProperty::MakeEnum(TEXT("Collision enabled mode"), { TEXT("NoCollision"), TEXT("QueryOnly"), TEXT("PhysicsOnly"), TEXT("QueryAndPhysics"), TEXT("ProbeOnly"), TEXT("QueryAndProbe") }));
	Schema.Add(TEXT("object_type"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Object channel, e.g. WorldDynamic or PhysicsBody")));
	Schema.Add(TEXT("all_channels_response"), FMcpSchemaProperty::MakeEnum(TEXT("Optional response applied to all channels"), { TEXT("Ignore"), TEXT("Overlap"), TEXT("Block") }));
	FMcpSchemaProperty ResponsesSchema;
	ResponsesSchema.Type = TEXT("array");
	ResponsesSchema.Description = TEXT("Per-channel response edits");
	ResponsesSchema.Items = MakeResponseSchema();
	Schema.Add(TEXT("responses"), ResponsesSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without mutating")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited map when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on failure")));
	return Schema;
}

FMcpToolResult UEditCollisionSettingsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

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

	ECollisionEnabled::Type CollisionEnabled = Component->GetCollisionEnabled();
	FString CollisionEnabledName;
	if (Arguments->TryGetStringField(TEXT("collision_enabled"), CollisionEnabledName) &&
		!PhysicsToolUtils::TryParseCollisionEnabled(CollisionEnabledName, CollisionEnabled, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ErrorMessage);
	}

	ECollisionChannel ObjectType = Component->GetCollisionObjectType();
	FString ObjectTypeName;
	if (Arguments->TryGetStringField(TEXT("object_type"), ObjectTypeName) &&
		!PhysicsToolUtils::TryParseCollisionChannel(ObjectTypeName, ObjectType, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ErrorMessage);
	}

	ECollisionResponse AllResponse = ECR_Block;
	FString AllResponseName;
	const bool bHasAllResponse = Arguments->TryGetStringField(TEXT("all_channels_response"), AllResponseName);
	if (bHasAllResponse && !PhysicsToolUtils::TryParseCollisionResponse(AllResponseName, AllResponse, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ErrorMessage);
	}

	const TArray<TSharedPtr<FJsonValue>>* Responses = nullptr;
	Arguments->TryGetArrayField(TEXT("responses"), Responses);

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Result = PhysicsToolUtils::SerializePrimitiveComponent(Component);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Result->SetBoolField(TEXT("would_change"), true);
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Collision edit dry run complete"));
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Collision Settings")));
	Actor->Modify();
	Component->Modify();

	FString ProfileName;
	if (Arguments->TryGetStringField(TEXT("profile_name"), ProfileName) && !ProfileName.IsEmpty())
	{
		Component->SetCollisionProfileName(FName(*ProfileName));
	}
	if (Arguments->HasField(TEXT("collision_enabled")))
	{
		Component->SetCollisionEnabled(CollisionEnabled);
	}
	if (Arguments->HasField(TEXT("object_type")))
	{
		Component->SetCollisionObjectType(ObjectType);
	}
	if (bHasAllResponse)
	{
		Component->SetCollisionResponseToAllChannels(AllResponse);
	}
	if (Responses)
	{
		for (int32 Index = 0; Index < Responses->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* ResponseObject = nullptr;
			if (!(*Responses)[Index].IsValid() || !(*Responses)[Index]->TryGetObject(ResponseObject) || !ResponseObject || !(*ResponseObject).IsValid())
			{
				if (Transaction.IsValid() && bRollbackOnError)
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("responses[%d] must be an object"), Index));
			}

			FString ChannelName;
			FString ResponseName;
			(*ResponseObject)->TryGetStringField(TEXT("channel"), ChannelName);
			(*ResponseObject)->TryGetStringField(TEXT("response"), ResponseName);
			ECollisionChannel Channel = ECC_WorldDynamic;
			ECollisionResponse Response = ECR_Block;
			if (!PhysicsToolUtils::TryParseCollisionChannel(ChannelName, Channel, ErrorMessage) ||
				!PhysicsToolUtils::TryParseCollisionResponse(ResponseName, Response, ErrorMessage))
			{
				if (Transaction.IsValid() && bRollbackOnError)
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ErrorMessage);
			}
			Component->SetCollisionResponseToChannel(Channel, Response);
		}
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
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Collision settings updated"));
}
