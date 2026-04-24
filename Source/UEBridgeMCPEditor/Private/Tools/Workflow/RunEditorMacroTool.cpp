// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/RunEditorMacroTool.h"

#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Project/QueryWorkspaceHealthTool.h"
#include "Tools/Workflow/RunProjectMaintenanceChecksTool.h"
#include "Utils/McpAssetModifier.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
	{
		return MakeShareable(new FJsonValueString(Value));
	}

	TArray<TSharedPtr<FJsonValue>> CopyStringArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(FieldName, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid())
				{
					Result.Add(MakeStringValue(Value->AsString()));
				}
			}
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildStep(const FString& Name, const bool bSuccess, const TSharedPtr<FJsonObject>& Payload)
	{
		TSharedPtr<FJsonObject> Step = MakeShareable(new FJsonObject);
		Step->SetStringField(TEXT("name"), Name);
		Step->SetBoolField(TEXT("success"), bSuccess);
		if (Payload.IsValid())
		{
			Step->SetObjectField(TEXT("payload"), Payload);
		}
		return Step;
	}

	void AddModifiedWorldAsset(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		if (World && World->PersistentLevel && World->PersistentLevel->GetPackage())
		{
			OutModifiedAssets.Add(MakeStringValue(World->PersistentLevel->GetPackage()->GetName()));
		}
	}

	FMcpToolResult RunCleanupGeneratedActorsMacro(
		const TSharedPtr<FJsonObject>& MacroArgs,
		const bool bDryRun)
	{
		const FString WorldType = MacroArgs.IsValid() && MacroArgs->HasTypedField<EJson::String>(TEXT("world"))
			? MacroArgs->GetStringField(TEXT("world"))
			: TEXT("editor");
		const FString LabelPrefix = MacroArgs.IsValid() && MacroArgs->HasTypedField<EJson::String>(TEXT("label_prefix"))
			? MacroArgs->GetStringField(TEXT("label_prefix"))
			: FString();
		const FString Tag = MacroArgs.IsValid() && MacroArgs->HasTypedField<EJson::String>(TEXT("tag"))
			? MacroArgs->GetStringField(TEXT("tag"))
			: TEXT("UEBridgeMCPGenerated");
		const int32 MaxActors = MacroArgs.IsValid() && MacroArgs->HasTypedField<EJson::Number>(TEXT("max_actors"))
			? FMath::Clamp(static_cast<int32>(MacroArgs->GetNumberField(TEXT("max_actors"))), 1, 1000)
			: 100;

		if (LabelPrefix.IsEmpty() && Tag.IsEmpty())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("cleanup_generated_actors requires label_prefix or tag"));
		}

		UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
		if (!World)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), FString::Printf(TEXT("No '%s' world is currently available"), *WorldType));
		}

		TArray<AActor*> MatchedActors;
		bool bTruncated = false;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			const bool bMatchesPrefix = !LabelPrefix.IsEmpty() && Actor->GetActorNameOrLabel().StartsWith(LabelPrefix, ESearchCase::IgnoreCase);
			const bool bMatchesTag = !Tag.IsEmpty() && Actor->Tags.Contains(FName(*Tag));
			if (!bMatchesPrefix && !bMatchesTag)
			{
				continue;
			}

			if (MatchedActors.Num() >= MaxActors)
			{
				bTruncated = true;
				continue;
			}
			MatchedActors.Add(Actor);
		}

		TArray<TSharedPtr<FJsonValue>> ActorArray;
		for (AActor* Actor : MatchedActors)
		{
			TSharedPtr<FJsonObject> ActorObject = MakeShareable(new FJsonObject);
			ActorObject->SetStringField(TEXT("actor_name"), Actor->GetName());
			ActorObject->SetStringField(TEXT("actor_label"), Actor->GetActorNameOrLabel());
			ActorObject->SetStringField(TEXT("path"), Actor->GetPathName());
			ActorArray.Add(MakeShareable(new FJsonValueObject(ActorObject)));
		}

		TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
		if (!bDryRun && MatchedActors.Num() > 0)
		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("Run Editor Macro: Cleanup Generated Actors")));
			for (AActor* Actor : MatchedActors)
			{
				if (IsValid(Actor))
				{
					Actor->Modify();
					World->DestroyActor(Actor);
				}
			}
			if (World->PersistentLevel)
			{
				World->PersistentLevel->MarkPackageDirty();
			}
			AddModifiedWorldAsset(World, ModifiedAssetsArray);
		}

		TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
		Summary->SetNumberField(TEXT("matched"), ActorArray.Num());
		Summary->SetNumberField(TEXT("deleted"), bDryRun ? 0 : ActorArray.Num());
		Summary->SetBoolField(TEXT("dry_run"), bDryRun);
		Summary->SetBoolField(TEXT("truncated"), bTruncated);

		TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
		Payload->SetStringField(TEXT("macro"), TEXT("cleanup_generated_actors"));
		Payload->SetBoolField(TEXT("success"), true);
		Payload->SetBoolField(TEXT("dry_run"), bDryRun);
		Payload->SetStringField(TEXT("world"), WorldType);
		Payload->SetStringField(TEXT("label_prefix"), LabelPrefix);
		Payload->SetStringField(TEXT("tag"), Tag);
		Payload->SetArrayField(TEXT("actors"), ActorArray);
		Payload->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		Payload->SetObjectField(TEXT("summary"), Summary);
		return FMcpToolResult::StructuredJson(Payload);
	}
}

FString URunEditorMacroTool::GetToolDescription() const
{
	return TEXT("Run a curated editor macro. This is intentionally not a universal script executor; v1 supports health collection, maintenance checks, Blueprint compile checks, and generated-actor cleanup.");
}

TMap<FString, FMcpSchemaProperty> URunEditorMacroTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("macro"), FMcpSchemaProperty::MakeEnum(
		TEXT("Curated macro id"),
		{
			TEXT("collect_workspace_health"),
			TEXT("run_maintenance_checks"),
			TEXT("compile_blueprint_assets"),
			TEXT("cleanup_generated_actors")
		},
		true));

	TSharedPtr<FJsonObject> ArgumentsRawSchema = MakeShareable(new FJsonObject);
	ArgumentsRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	ArgumentsRawSchema->SetBoolField(TEXT("additionalProperties"), true);
	FMcpSchemaProperty ArgumentsSchema;
	ArgumentsSchema.Description = TEXT("Macro-specific arguments. No arbitrary script text is accepted.");
	ArgumentsSchema.RawSchema = ArgumentsRawSchema;
	Schema.Add(TEXT("arguments"), ArgumentsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate and report planned work where the selected macro supports it.")));
	return Schema;
}

FMcpToolResult URunEditorMacroTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString Macro = GetStringArgOrDefault(Arguments, TEXT("macro"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	const TSharedPtr<FJsonObject>* MacroArgsPtr = nullptr;
	Arguments->TryGetObjectField(TEXT("arguments"), MacroArgsPtr);
	TSharedPtr<FJsonObject> MacroArgs = (MacroArgsPtr && (*MacroArgsPtr).IsValid())
		? *MacroArgsPtr
		: MakeShareable(new FJsonObject);

	TArray<TSharedPtr<FJsonValue>> StepsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	bool bAnyFailure = false;

	if (Macro == TEXT("collect_workspace_health"))
	{
		UQueryWorkspaceHealthTool* Tool = NewObject<UQueryWorkspaceHealthTool>();
		const FMcpToolResult Result = Tool->Execute(MacroArgs, Context);
		const TSharedPtr<FJsonObject> Payload = Result.GetStructuredContent();
		StepsArray.Add(MakeShareable(new FJsonValueObject(BuildStep(TEXT("query-workspace-health"), Result.bSuccess, Payload))));
		bAnyFailure = !Result.bSuccess;
		if (!Result.bSuccess && Payload.IsValid())
		{
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(Payload)));
		}
	}
	else if (Macro == TEXT("run_maintenance_checks"))
	{
		MacroArgs->SetBoolField(TEXT("dry_run"), bDryRun || (MacroArgs->HasTypedField<EJson::Boolean>(TEXT("dry_run")) && MacroArgs->GetBoolField(TEXT("dry_run"))));
		URunProjectMaintenanceChecksTool* Tool = NewObject<URunProjectMaintenanceChecksTool>();
		const FMcpToolResult Result = Tool->Execute(MacroArgs, Context);
		const TSharedPtr<FJsonObject> Payload = Result.GetStructuredContent();
		StepsArray.Add(MakeShareable(new FJsonValueObject(BuildStep(TEXT("run-project-maintenance-checks"), Result.bSuccess, Payload))));
		bAnyFailure = !Result.bSuccess;
		if (!Result.bSuccess && Payload.IsValid())
		{
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(Payload)));
		}
	}
	else if (Macro == TEXT("compile_blueprint_assets"))
	{
		const TArray<TSharedPtr<FJsonValue>> AssetPaths = CopyStringArrayField(MacroArgs, TEXT("asset_paths"));
		if (AssetPaths.Num() == 0)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("arguments.asset_paths is required for compile_blueprint_assets"));
		}

		if (bDryRun)
		{
			TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
			Payload->SetStringField(TEXT("tool_name"), TEXT("compile-assets"));
			Payload->SetArrayField(TEXT("asset_paths"), AssetPaths);
			Payload->SetBoolField(TEXT("planned"), true);
			StepsArray.Add(MakeShareable(new FJsonValueObject(BuildStep(TEXT("compile-assets"), true, Payload))));
		}
		else
		{
			TSharedPtr<FJsonObject> CompileArgs = MakeShareable(new FJsonObject);
			CompileArgs->SetArrayField(TEXT("asset_paths"), AssetPaths);
			CompileArgs->SetStringField(TEXT("mode"), TEXT("auto"));
			CompileArgs->SetBoolField(TEXT("include_diagnostics"), true);

			UCompileAssetsTool* Tool = NewObject<UCompileAssetsTool>();
			const FMcpToolResult Result = Tool->Execute(CompileArgs, Context);
			const TSharedPtr<FJsonObject> Payload = Result.GetStructuredContent();
			StepsArray.Add(MakeShareable(new FJsonValueObject(BuildStep(TEXT("compile-assets"), Result.bSuccess, Payload))));
			bAnyFailure = !Result.bSuccess;
			if (!Result.bSuccess && Payload.IsValid())
			{
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(Payload)));
			}
		}
	}
	else if (Macro == TEXT("cleanup_generated_actors"))
	{
		const FMcpToolResult Result = RunCleanupGeneratedActorsMacro(MacroArgs, bDryRun);
		const TSharedPtr<FJsonObject> Payload = Result.GetStructuredContent();
		StepsArray.Add(MakeShareable(new FJsonValueObject(BuildStep(TEXT("cleanup_generated_actors"), Result.bSuccess, Payload))));
		bAnyFailure = !Result.bSuccess;
		if (Payload.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* StepModifiedAssets = nullptr;
			if (Payload->TryGetArrayField(TEXT("modified_assets"), StepModifiedAssets) && StepModifiedAssets)
			{
				for (const TSharedPtr<FJsonValue>& ModifiedAsset : *StepModifiedAssets)
				{
					ModifiedAssetsArray.Add(ModifiedAsset);
				}
			}
		}
		if (!Result.bSuccess && Payload.IsValid())
		{
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(Payload)));
		}
	}
	else
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), FString::Printf(TEXT("Unsupported editor macro '%s'"), *Macro));
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("step_count"), StepsArray.Num());
	Summary->SetBoolField(TEXT("dry_run"), bDryRun);
	Summary->SetBoolField(TEXT("success"), !bAnyFailure);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailure);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("macro"), Macro);
	Response->SetObjectField(TEXT("arguments"), MacroArgs);
	Response->SetArrayField(TEXT("steps"), StepsArray);
	Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredJson(Response, bAnyFailure);
}
