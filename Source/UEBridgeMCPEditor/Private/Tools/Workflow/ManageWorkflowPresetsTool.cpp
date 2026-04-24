// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/ManageWorkflowPresetsTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Tools/Workflow/WorkflowPresetUtils.h"

namespace
{
	TSharedPtr<FJsonObject> BuildResultEnvelope(
		const FString& ToolName,
		const bool bSuccess,
		const TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		const TArray<TSharedPtr<FJsonValue>>& WarningsArray,
		const TArray<TSharedPtr<FJsonValue>>& DiagnosticsArray,
		const TArray<TSharedPtr<FJsonValue>>& ModifiedAssetsArray,
		const TArray<TSharedPtr<FJsonValue>>& PartialResultsArray)
	{
		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), ToolName);
		Response->SetBoolField(TEXT("success"), bSuccess);
		Response->SetArrayField(TEXT("results"), ResultsArray);
		Response->SetArrayField(TEXT("warnings"), WarningsArray);
		Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
		Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
		Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
		return Response;
	}
}

FString UManageWorkflowPresetsTool::GetToolDescription() const
{
	return TEXT("Manage project workflow presets stored under Config/UEBridgeMCP/WorkflowPresets with list, upsert, and delete operations.");
}

TMap<FString, FMcpSchemaProperty> UManageWorkflowPresetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Workflow preset operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Preset management action"),
		{ TEXT("list_presets"), TEXT("upsert_preset"), TEXT("delete_preset") },
		true)));
	OperationSchema->Properties.Add(TEXT("preset"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Preset object for upsert_preset"))));
	OperationSchema->Properties.Add(TEXT("preset_id"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Preset id for delete_preset"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Preset operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without writing preset files")));
	return Schema;
}

FMcpToolResult UManageWorkflowPresetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bSuccess = false;
		bool bChanged = false;
		FString Error;

		if (Action == TEXT("list_presets"))
		{
			TArray<TSharedPtr<FJsonObject>> Presets;
			if (WorkflowPresetUtils::ListPresets(Presets, Error))
			{
				TArray<TSharedPtr<FJsonValue>> PresetArray;
				for (const TSharedPtr<FJsonObject>& Preset : Presets)
				{
					PresetArray.Add(MakeShareable(new FJsonValueObject(Preset)));
				}
				ResultObject->SetArrayField(TEXT("presets"), PresetArray);
				ResultObject->SetNumberField(TEXT("preset_count"), Presets.Num());
				bSuccess = true;
			}
		}
		else if (Action == TEXT("upsert_preset"))
		{
			const TSharedPtr<FJsonObject>* PresetObject = nullptr;
			if (!(*OperationObject)->TryGetObjectField(TEXT("preset"), PresetObject) || !PresetObject || !(*PresetObject).IsValid())
			{
				Error = TEXT("'preset' object is required for upsert_preset");
			}
			else
			{
				FString ValidationError;
				if (!WorkflowPresetUtils::ValidatePresetObject(*PresetObject, ValidationError))
				{
					Error = ValidationError;
				}
				else
				{
					FString PresetPath;
					(*PresetObject)->TryGetStringField(TEXT("id"), PresetPath);
					if (bDryRun)
					{
						ResultObject->SetStringField(TEXT("preset_id"), (*PresetObject)->GetStringField(TEXT("id")));
						ResultObject->SetBoolField(TEXT("would_write"), true);
						bSuccess = true;
					}
					else
					{
						FString SavedPath;
						if (WorkflowPresetUtils::SavePreset(*PresetObject, SavedPath, Error))
						{
							ResultObject->SetStringField(TEXT("preset_id"), (*PresetObject)->GetStringField(TEXT("id")));
							ResultObject->SetStringField(TEXT("preset_path"), SavedPath);
							ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(SavedPath)));
							bSuccess = true;
							bChanged = true;
						}
					}
				}
			}
		}
		else if (Action == TEXT("delete_preset"))
		{
			FString PresetId;
			if (!(*OperationObject)->TryGetStringField(TEXT("preset_id"), PresetId) || PresetId.IsEmpty())
			{
				Error = TEXT("'preset_id' is required for delete_preset");
			}
			else if (bDryRun)
			{
				ResultObject->SetStringField(TEXT("preset_id"), PresetId);
				ResultObject->SetBoolField(TEXT("would_delete"), true);
				bSuccess = true;
			}
			else
			{
				FString DeletedPath;
				if (WorkflowPresetUtils::DeletePreset(PresetId, DeletedPath, Error))
				{
					ResultObject->SetStringField(TEXT("preset_id"), PresetId);
					ResultObject->SetStringField(TEXT("preset_path"), DeletedPath);
					ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(DeletedPath)));
					bSuccess = true;
					bChanged = true;
				}
			}
		}
		else
		{
			Error = FString::Printf(TEXT("Unsupported action: '%s'"), *Action);
		}

		ResultObject->SetBoolField(TEXT("success"), bSuccess);
		ResultObject->SetBoolField(TEXT("changed"), bChanged);
		if (!Error.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("error"), Error);
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	return FMcpToolResult::StructuredJson(BuildResultEnvelope(
		GetToolName(),
		!bAnyFailed,
		ResultsArray,
		WarningsArray,
		DiagnosticsArray,
		ModifiedAssetsArray,
		PartialResultsArray), bAnyFailed);
}
