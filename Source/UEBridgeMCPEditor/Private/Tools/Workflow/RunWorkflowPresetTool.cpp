// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/RunWorkflowPresetTool.h"

#include "Protocol/McpPromptRegistry.h"
#include "Protocol/McpResourceRegistry.h"
#include "Tools/McpToolRegistry.h"
#include "Tools/Workflow/WorkflowPresetUtils.h"

namespace
{
	TSharedPtr<FJsonObject> BuildStepResult(const FString& ToolName, const FMcpToolResult& Result)
	{
		TSharedPtr<FJsonObject> StepObject = MakeShareable(new FJsonObject);
		StepObject->SetStringField(TEXT("tool_name"), ToolName);
		StepObject->SetBoolField(TEXT("success"), Result.bSuccess);
		StepObject->SetBoolField(TEXT("is_error"), Result.bIsError);
		StepObject->SetObjectField(TEXT("response"), Result.ToJson());
		return StepObject;
	}

	TSharedPtr<FJsonObject> ResolveArgumentsObject(const TSharedPtr<FJsonObject>& SourceObject, const TSharedPtr<FJsonObject>& RuntimeArguments)
	{
		TSharedPtr<FJsonObject> ResolvedObject = MakeShareable(new FJsonObject);
		if (!SourceObject.IsValid())
		{
			return ResolvedObject;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SourceObject->Values)
		{
			ResolvedObject->SetField(Pair.Key, WorkflowPresetUtils::ResolveTemplates(Pair.Value, RuntimeArguments));
		}
		return ResolvedObject;
	}
}

FString URunWorkflowPresetTool::GetToolDescription() const
{
	return TEXT("Expand or execute a project workflow preset, resolving built-in resources, prompts, default arguments, and sequential tool calls.");
}

TMap<FString, FMcpSchemaProperty> URunWorkflowPresetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("preset_id"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Workflow preset id"), true));
	Schema.Add(TEXT("arguments"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Runtime arguments merged over preset default_arguments")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Expand prompt/resources/tool plan without executing tools")));
	Schema.Add(TEXT("stop_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Stop execution when a tool call fails")));
	return Schema;
}

FMcpToolResult URunWorkflowPresetTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString PresetId = GetStringArgOrDefault(Arguments, TEXT("preset_id"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bStopOnError = GetBoolArgOrDefault(Arguments, TEXT("stop_on_error"), true);

	TSharedPtr<FJsonObject> PresetObject;
	FString PresetPath;
	FString LoadError;
	if (!WorkflowPresetUtils::LoadPreset(PresetId, PresetObject, PresetPath, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_PRESET_NOT_FOUND"), LoadError);
	}

	const TSharedPtr<FJsonObject>* DefaultArguments = nullptr;
	PresetObject->TryGetObjectField(TEXT("default_arguments"), DefaultArguments);

	const TSharedPtr<FJsonObject>* RuntimeArgumentOverrides = nullptr;
	Arguments->TryGetObjectField(TEXT("arguments"), RuntimeArgumentOverrides);
	TSharedPtr<FJsonObject> EffectiveArguments = WorkflowPresetUtils::MergeArguments(
		DefaultArguments ? *DefaultArguments : MakeShareable(new FJsonObject),
		RuntimeArgumentOverrides ? *RuntimeArgumentOverrides : MakeShareable(new FJsonObject));

	TArray<TSharedPtr<FJsonValue>> ResourceArray;
	const TArray<TSharedPtr<FJsonValue>>* ResourceUris = nullptr;
	PresetObject->TryGetArrayField(TEXT("resource_uris"), ResourceUris);
	if (ResourceUris)
	{
		for (const TSharedPtr<FJsonValue>& UriValue : *ResourceUris)
		{
			const FString Uri = UriValue.IsValid() ? UriValue->AsString() : FString();
			if (Uri.IsEmpty())
			{
				continue;
			}

			FMcpResourceReadResult ResourceResult;
			FString ResourceError;
			if (!FMcpResourceRegistry::Get().ReadResource(Uri, ResourceResult, ResourceError))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_RESOURCE_READ_FAILED"), ResourceError);
			}

			ResourceArray.Add(MakeShareable(new FJsonValueObject(ResourceResult.ToJson())));
		}
	}

	FString PromptName;
	PresetObject->TryGetStringField(TEXT("prompt_name"), PromptName);

	FMcpPromptGetResult PromptResult;
	if (!PromptName.IsEmpty())
	{
		FString PromptError;
		if (!FMcpPromptRegistry::Get().BuildPrompt(PromptName, EffectiveArguments, PromptResult, PromptError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROMPT_BUILD_FAILED"), PromptError);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
	PresetObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls);

	TArray<TSharedPtr<FJsonValue>> PlannedToolCalls;
	TArray<TSharedPtr<FJsonValue>> StepResults;
	bool bAnyFailure = false;

	if (ToolCalls)
	{
		for (int32 ToolCallIndex = 0; ToolCallIndex < ToolCalls->Num(); ++ToolCallIndex)
		{
			const TSharedPtr<FJsonObject>* ToolCallObject = nullptr;
			if (!(*ToolCalls)[ToolCallIndex].IsValid() || !(*ToolCalls)[ToolCallIndex]->TryGetObject(ToolCallObject) || !ToolCallObject || !(*ToolCallObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("tool_calls[%d] must be an object"), ToolCallIndex));
			}

			FString ToolName;
			if (!(*ToolCallObject)->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("tool_calls[%d].name is required"), ToolCallIndex));
			}

			if (ToolName == GetToolName())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("run-workflow-preset cannot recursively invoke itself"));
			}

			const TSharedPtr<FJsonObject>* ToolArguments = nullptr;
			(*ToolCallObject)->TryGetObjectField(TEXT("arguments"), ToolArguments);
			TSharedPtr<FJsonObject> ResolvedArguments = ResolveArgumentsObject(
				ToolArguments ? *ToolArguments : MakeShareable(new FJsonObject),
				EffectiveArguments);

			TSharedPtr<FJsonObject> PlannedCall = MakeShareable(new FJsonObject);
			PlannedCall->SetStringField(TEXT("name"), ToolName);
			PlannedCall->SetObjectField(TEXT("arguments"), ResolvedArguments);
			PlannedToolCalls.Add(MakeShareable(new FJsonValueObject(PlannedCall)));

			if (!bDryRun)
			{
				FMcpToolResult ToolResult = FMcpToolRegistry::Get().ExecuteTool(ToolName, ResolvedArguments, Context);
				StepResults.Add(MakeShareable(new FJsonValueObject(BuildStepResult(ToolName, ToolResult))));
				if (ToolResult.bIsError || !ToolResult.bSuccess)
				{
					bAnyFailure = true;
					if (bStopOnError)
					{
						break;
					}
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailure);
	Response->SetStringField(TEXT("preset_id"), PresetId);
	Response->SetStringField(TEXT("preset_path"), PresetPath);
	Response->SetObjectField(TEXT("effective_arguments"), EffectiveArguments);
	Response->SetArrayField(TEXT("resources"), ResourceArray);
	Response->SetObjectField(TEXT("prompt"), PromptResult.ToJson());
	Response->SetArrayField(TEXT("tool_calls"), PlannedToolCalls);
	Response->SetArrayField(TEXT("steps"), StepResults);
	Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), bAnyFailure ? StepResults : TArray<TSharedPtr<FJsonValue>>());
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	return FMcpToolResult::StructuredJson(Response, bAnyFailure);
}
