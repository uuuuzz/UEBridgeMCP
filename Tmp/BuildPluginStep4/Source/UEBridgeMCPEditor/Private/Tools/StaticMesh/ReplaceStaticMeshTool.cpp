// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/StaticMesh/ReplaceStaticMeshTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	TSharedPtr<FJsonObject> BuildPartialFailurePayload(
		const FString& ToolName,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> PartialObject = MakeShareable(new FJsonObject);
		PartialObject->SetStringField(TEXT("tool"), ToolName);
		PartialObject->SetArrayField(TEXT("results"), ResultsArray);
		PartialObject->SetArrayField(TEXT("warnings"), WarningsArray);
		PartialObject->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		PartialObject->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		PartialObject->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return PartialObject;
	}
}

FString UReplaceStaticMeshTool::GetToolDescription() const
{
	return TEXT("Replace static mesh component instances on selected actors without changing asset defaults.");
}

TMap<FString, FMcpSchemaProperty> UReplaceStaticMeshTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("static_mesh_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Replacement static mesh asset path"), true));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional static mesh component name filter")));

	TSharedPtr<FMcpSchemaProperty> ActorReferenceSchema = MakeShared<FMcpSchemaProperty>();
	ActorReferenceSchema->Type = TEXT("object");
	ActorReferenceSchema->Description = TEXT("Actor reference using actor_name or actor_handle");
	ActorReferenceSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label"))));
	ActorReferenceSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle from level queries"))));

	FMcpSchemaProperty ActorsSchema;
	ActorsSchema.Type = TEXT("array");
	ActorsSchema.Description = TEXT("Actors whose static mesh components should be replaced");
	ActorsSchema.bRequired = true;
	ActorsSchema.Items = ActorReferenceSchema;
	Schema.Add(TEXT("actors"), ActorsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the current editor world after edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel transaction on first failure")));

	return Schema;
}

TArray<FString> UReplaceStaticMeshTool::GetRequiredParams() const
{
	return { TEXT("actors"), TEXT("static_mesh_path") };
}

FMcpToolResult UReplaceStaticMeshTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString StaticMeshPath = GetStringArgOrDefault(Arguments, TEXT("static_mesh_path"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* ActorReferences = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("actors"), ActorReferences) || !ActorReferences || ActorReferences->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actors' array is required"));
	}

	FString LoadError;
	UStaticMesh* ReplacementMesh = FMcpAssetModifier::LoadAssetByPath<UStaticMesh>(StaticMeshPath, LoadError);
	if (!ReplacementMesh)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UWorld* World = nullptr;
	TArray<AActor*> Actors;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	if (!LevelActorToolUtils::ResolveActorReferences(*ActorReferences, WorldType, Context, World, Actors, ErrorCode, ErrorMessage, ErrorDetails))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Replace Static Mesh")));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
	{
		AActor* Actor = Actors[ActorIndex];
		TSharedPtr<FJsonObject> ActorResult = MakeShareable(new FJsonObject);
		ActorResult->SetNumberField(TEXT("index"), ActorIndex);
		ActorResult->SetObjectField(TEXT("actor"), McpV2ToolUtils::SerializeActorSummary(Actor, Context.SessionId, false, false));

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
		if (!ComponentName.IsEmpty())
		{
			StaticMeshComponents = StaticMeshComponents.FilterByPredicate([&ComponentName](UStaticMeshComponent* Component)
			{
				return Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase);
			});
		}

		if (StaticMeshComponents.Num() == 0)
		{
			ActorResult->SetBoolField(TEXT("success"), false);
			ActorResult->SetStringField(TEXT("error"), ComponentName.IsEmpty()
				? TEXT("Actor has no static mesh components")
				: FString::Printf(TEXT("Static mesh component '%s' was not found"), *ComponentName));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));
			bAnyFailed = true;
			if (!bDryRun && bRollbackOnError && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_COMPONENT_NOT_FOUND"),
					ActorResult->GetStringField(TEXT("error")),
					nullptr,
					BuildPartialFailurePayload(GetToolName(), ResultsArray, WarningsArray, DiagnosticsArray, ModifiedAssetsArray, PartialResultsArray));
			}

			ResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> ComponentResults;
		bool bActorChanged = false;
		for (UStaticMeshComponent* Component : StaticMeshComponents)
		{
			TSharedPtr<FJsonObject> ComponentResult = MakeShareable(new FJsonObject);
			ComponentResult->SetStringField(TEXT("component_name"), Component->GetName());
			ComponentResult->SetStringField(TEXT("before_static_mesh"), Component->GetStaticMesh() ? Component->GetStaticMesh()->GetPathName() : TEXT(""));
			ComponentResult->SetStringField(TEXT("after_static_mesh"), StaticMeshPath);
			const bool bChanged = Component->GetStaticMesh() != ReplacementMesh;
			ComponentResult->SetBoolField(TEXT("changed"), bChanged);
			ComponentResult->SetBoolField(TEXT("success"), true);
			ComponentResults.Add(MakeShareable(new FJsonValueObject(ComponentResult)));

			if (!bDryRun && bChanged)
			{
				Component->Modify();
				Component->SetStaticMesh(ReplacementMesh);
			}

			bActorChanged = bActorChanged || bChanged;
		}

		ActorResult->SetBoolField(TEXT("success"), true);
		ActorResult->SetBoolField(TEXT("changed"), bActorChanged);
		ActorResult->SetArrayField(TEXT("components"), ComponentResults);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ActorResult)));
	}

	if (!LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, WarningsArray, ModifiedAssetsArray, ErrorCode, ErrorMessage))
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("world_name"), World ? World->GetName() : TEXT(""));
	Response->SetStringField(TEXT("world_path"), World ? World->GetPathName() : TEXT(""));
	Response->SetStringField(TEXT("static_mesh_path"), StaticMeshPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
