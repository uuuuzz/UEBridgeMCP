// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/BlueprintScaffoldFromSpecTool.h"
#include "Tools/Blueprint/EditBlueprintComponentsTool.h"
#include "Tools/Blueprint/EditBlueprintMembersTool.h"
#include "Tools/Asset/CompileAssetsTool.h"
#include "Tools/Write/AddGraphNodeTool.h"
#include "Tools/Write/ConnectGraphPinsTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Kismet2/KismetEditorUtilities.h"
namespace BlueprintScaffoldFromSpecToolPrivate
{
	TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& SourceObject)
	{
		TSharedPtr<FJsonObject> ClonedObject = MakeShareable(new FJsonObject);
		if (!SourceObject.IsValid())
		{
			return ClonedObject;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : SourceObject->Values)
		{
			ClonedObject->SetField(Pair.Key, Pair.Value);
		}

		return ClonedObject;
	}

	TSharedPtr<FJsonObject> ExtractDataPayload(const FMcpToolResult& ToolResult)
	{
		if (const TSharedPtr<FJsonObject> StructuredContent = ToolResult.GetStructuredContent(); StructuredContent.IsValid())
		{
			return StructuredContent;
		}

		return nullptr;
	}

	void AppendStringArrayAsWarnings(const TArray<FString>& InWarnings, TArray<TSharedPtr<FJsonValue>>& OutWarnings)
	{
		for (const FString& Warning : InWarnings)
		{
			OutWarnings.Add(MakeShareable(new FJsonValueString(Warning)));
		}
	}

	void AppendArrayField(const TSharedPtr<FJsonObject>& SourceObject, const FString& FieldName, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		if (!SourceObject.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (SourceObject->TryGetArrayField(FieldName, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				OutArray.Add(Value);
			}
		}
	}

	FString GetStringFieldOrDefault(const TSharedPtr<FJsonObject>& SourceObject, const FString& FieldName, const FString& DefaultValue = TEXT(""))
	{
		if (!SourceObject.IsValid())
		{
			return DefaultValue;
		}

		FString Value;
		return SourceObject->TryGetStringField(FieldName, Value) ? Value : DefaultValue;
	}

	void AppendNodeId(TMap<FString, FString>& NodeIdsByAlias, const TSharedPtr<FJsonObject>& StepPayload)
	{
		if (!StepPayload.IsValid())
		{
			return;
		}

		FString Alias;
		FString NodeGuid;
		if (StepPayload->TryGetStringField(TEXT("alias"), Alias)
			&& !Alias.IsEmpty()
			&& StepPayload->TryGetStringField(TEXT("node_guid"), NodeGuid)
			&& !NodeGuid.IsEmpty())
		{
			NodeIdsByAlias.Add(Alias, NodeGuid);
		}
	}

	TSharedPtr<FJsonObject> CreateFailedStep(
		const FString& StepName,
		const FString& ErrorMessage,
		const TSharedPtr<FJsonObject>& StepPayload = nullptr)
	{
		TSharedPtr<FJsonObject> FailedStep = StepPayload.IsValid() ? CloneJsonObject(StepPayload) : MakeShareable(new FJsonObject);
		FailedStep->SetStringField(TEXT("step"), StepName);
		FailedStep->SetBoolField(TEXT("success"), false);
		FailedStep->SetStringField(TEXT("error"), ErrorMessage);
		return FailedStep;
	}

	bool InvokeMembersTool(
		const FString& AssetPath,
		const TArray<TSharedPtr<FJsonValue>>& MemberActions,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		FString& OutError)
	{
		if (MemberActions.Num() == 0)
		{
			return true;
		}

		TSharedPtr<FJsonObject> MembersArgs = MakeShareable(new FJsonObject);
		MembersArgs->SetStringField(TEXT("asset_path"), AssetPath);
		MembersArgs->SetStringField(TEXT("compile"), TEXT("never"));
		MembersArgs->SetArrayField(TEXT("actions"), MemberActions);

		UEditBlueprintMembersTool* MembersTool = NewObject<UEditBlueprintMembersTool>();
		const FMcpToolResult MembersResult = MembersTool->Execute(MembersArgs, Context);
		const TSharedPtr<FJsonObject> MembersPayload = ExtractDataPayload(MembersResult);

		TSharedPtr<FJsonObject> StepObject = MakeShareable(new FJsonObject);
		StepObject->SetStringField(TEXT("step"), TEXT("edit_members"));
		StepObject->SetBoolField(TEXT("success"), MembersResult.bSuccess);
		if (MembersPayload.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ResultsArray = nullptr;
			if (MembersPayload->TryGetArrayField(TEXT("results"), ResultsArray) && ResultsArray)
			{
				StepObject->SetArrayField(TEXT("results"), *ResultsArray);
			}
		}
		OutSteps.Add(MakeShareable(new FJsonValueObject(StepObject)));

		AppendArrayField(MembersPayload, TEXT("warnings"), OutWarnings);
		AppendArrayField(MembersPayload, TEXT("diagnostics"), OutDiagnostics);
		AppendArrayField(MembersPayload, TEXT("modified_assets"), OutModifiedAssets);

		if (!MembersResult.bSuccess)
		{
			OutPartialResults.Add(MakeShareable(new FJsonValueObject(StepObject)));
			OutError = TEXT("edit-blueprint-members failed");
			return false;
		}

		return true;
	}

	bool InvokeComponentsTool(
		const FString& AssetPath,
		const TArray<TSharedPtr<FJsonValue>>& ComponentActions,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		FString& OutError)
	{
		if (ComponentActions.Num() == 0)
		{
			return true;
		}

		TSharedPtr<FJsonObject> ComponentsArgs = MakeShareable(new FJsonObject);
		ComponentsArgs->SetStringField(TEXT("asset_path"), AssetPath);
		ComponentsArgs->SetStringField(TEXT("compile"), TEXT("never"));
		ComponentsArgs->SetArrayField(TEXT("actions"), ComponentActions);

		UEditBlueprintComponentsTool* ComponentsTool = NewObject<UEditBlueprintComponentsTool>();
		const FMcpToolResult ComponentsResult = ComponentsTool->Execute(ComponentsArgs, Context);
		const TSharedPtr<FJsonObject> ComponentsPayload = ExtractDataPayload(ComponentsResult);

		TSharedPtr<FJsonObject> StepObject = MakeShareable(new FJsonObject);
		StepObject->SetStringField(TEXT("step"), TEXT("edit_components"));
		StepObject->SetBoolField(TEXT("success"), ComponentsResult.bSuccess);
		if (ComponentsPayload.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ResultsArray = nullptr;
			if (ComponentsPayload->TryGetArrayField(TEXT("results"), ResultsArray) && ResultsArray)
			{
				StepObject->SetArrayField(TEXT("results"), *ResultsArray);
			}
		}
		OutSteps.Add(MakeShareable(new FJsonValueObject(StepObject)));

		AppendArrayField(ComponentsPayload, TEXT("warnings"), OutWarnings);
		AppendArrayField(ComponentsPayload, TEXT("diagnostics"), OutDiagnostics);
		AppendArrayField(ComponentsPayload, TEXT("modified_assets"), OutModifiedAssets);

		if (!ComponentsResult.bSuccess)
		{
			OutPartialResults.Add(MakeShareable(new FJsonValueObject(StepObject)));
			OutError = TEXT("edit-blueprint-components failed");
			return false;
		}

		return true;
	}

	bool InvokeEventGraphSteps(
		const FString& AssetPath,
		const TArray<TSharedPtr<FJsonValue>>& EventGraphSteps,
		const FMcpToolContext& Context,
		TArray<TSharedPtr<FJsonValue>>& OutSteps,
		TArray<TSharedPtr<FJsonValue>>& OutWarnings,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets,
		TArray<TSharedPtr<FJsonValue>>& OutPartialResults,
		FString& OutError)
	{
		if (EventGraphSteps.Num() == 0)
		{
			return true;
		}

		UAddGraphNodeTool* AddGraphNodeTool = NewObject<UAddGraphNodeTool>();
		UConnectGraphPinsTool* ConnectGraphPinsTool = NewObject<UConnectGraphPinsTool>();
		TMap<FString, FString> NodeIdsByAlias;

		for (int32 StepIndex = 0; StepIndex < EventGraphSteps.Num(); ++StepIndex)
		{
			const TSharedPtr<FJsonObject>* EventGraphStepObject = nullptr;
			if (!EventGraphSteps[StepIndex]->TryGetObject(EventGraphStepObject) || !(*EventGraphStepObject).IsValid())
			{
				continue;
			}

			const FString StepType = GetStringFieldOrDefault(*EventGraphStepObject, TEXT("step"));
			if (StepType == TEXT("add_node"))
			{
				TSharedPtr<FJsonObject> AddNodeArgs = CloneJsonObject(*EventGraphStepObject);
				AddNodeArgs->SetStringField(TEXT("asset_path"), AssetPath);
				if (!AddNodeArgs->HasField(TEXT("graph_name")))
				{
					AddNodeArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				}

				const FMcpToolResult AddNodeResult = AddGraphNodeTool->Execute(AddNodeArgs, Context);
				const TSharedPtr<FJsonObject> AddNodePayload = ExtractDataPayload(AddNodeResult);
				TSharedPtr<FJsonObject> StepObject = AddNodePayload.IsValid() ? CloneJsonObject(AddNodePayload) : MakeShareable(new FJsonObject);
				StepObject->SetStringField(TEXT("step"), TEXT("add_node"));
				StepObject->SetBoolField(TEXT("success"), AddNodeResult.bSuccess);
				if ((*EventGraphStepObject)->HasField(TEXT("alias")))
				{
					StepObject->SetField(TEXT("alias"), (*EventGraphStepObject)->TryGetField(TEXT("alias")));
				}
				OutSteps.Add(MakeShareable(new FJsonValueObject(StepObject)));
				AppendNodeId(NodeIdsByAlias, StepObject);

				if (!AddNodeResult.bSuccess)
				{
					OutPartialResults.Add(MakeShareable(new FJsonValueObject(StepObject)));
					OutError = TEXT("add-graph-node failed during event_graph scaffolding");
					return false;
				}

				OutModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
			}
			else if (StepType == TEXT("connect"))
			{
				TSharedPtr<FJsonObject> ConnectArgs = MakeShareable(new FJsonObject);
				ConnectArgs->SetStringField(TEXT("asset_path"), AssetPath);

				FString SourceNode = GetStringFieldOrDefault(*EventGraphStepObject, TEXT("source_node"));
				FString TargetNode = GetStringFieldOrDefault(*EventGraphStepObject, TEXT("target_node"));
				const FString SourceAlias = GetStringFieldOrDefault(*EventGraphStepObject, TEXT("source_alias"));
				const FString TargetAlias = GetStringFieldOrDefault(*EventGraphStepObject, TEXT("target_alias"));
				if (!SourceAlias.IsEmpty() && NodeIdsByAlias.Contains(SourceAlias))
				{
					SourceNode = NodeIdsByAlias[SourceAlias];
				}
				if (!TargetAlias.IsEmpty() && NodeIdsByAlias.Contains(TargetAlias))
				{
					TargetNode = NodeIdsByAlias[TargetAlias];
				}

				ConnectArgs->SetStringField(TEXT("source_node"), SourceNode);
				ConnectArgs->SetStringField(TEXT("source_pin"), GetStringFieldOrDefault(*EventGraphStepObject, TEXT("source_pin")));
				ConnectArgs->SetStringField(TEXT("target_node"), TargetNode);
				ConnectArgs->SetStringField(TEXT("target_pin"), GetStringFieldOrDefault(*EventGraphStepObject, TEXT("target_pin")));

				const FMcpToolResult ConnectResult = ConnectGraphPinsTool->Execute(ConnectArgs, Context);
				const TSharedPtr<FJsonObject> ConnectPayload = ExtractDataPayload(ConnectResult);
				TSharedPtr<FJsonObject> StepObject = ConnectPayload.IsValid() ? CloneJsonObject(ConnectPayload) : MakeShareable(new FJsonObject);
				StepObject->SetStringField(TEXT("step"), TEXT("connect"));
				StepObject->SetBoolField(TEXT("success"), ConnectResult.bSuccess);
				if (!SourceAlias.IsEmpty())
				{
					StepObject->SetStringField(TEXT("source_alias"), SourceAlias);
				}
				if (!TargetAlias.IsEmpty())
				{
					StepObject->SetStringField(TEXT("target_alias"), TargetAlias);
				}
				OutSteps.Add(MakeShareable(new FJsonValueObject(StepObject)));

				if (!ConnectResult.bSuccess)
				{
					OutPartialResults.Add(MakeShareable(new FJsonValueObject(StepObject)));
					OutError = TEXT("connect-graph-pins failed during event_graph scaffolding");
					return false;
				}

				OutModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
			}
			else
			{
				OutWarnings.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Unsupported event_graph step '%s' was ignored"), *StepType))));
			}
		}

		return true;
	}
}

FString UBlueprintScaffoldFromSpecTool::GetToolDescription() const
{
	return TEXT("High-level Blueprint scaffolding that creates or merges a Blueprint from a spec containing components, variables, functions, and event_graph steps. "
		"Orchestrates edit-blueprint-members, edit-blueprint-components, add-graph-node, connect-graph-pins, and compile-assets internally.");
}

TMap<FString, FMcpSchemaProperty> UBlueprintScaffoldFromSpecTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target Blueprint asset path"), true));
	Schema.Add(TEXT("parent_class"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Parent class path (for new assets), e.g. '/Script/Engine.Actor'"), true));
	Schema.Add(TEXT("overwrite_mode"), FMcpSchemaProperty::MakeEnum(
		TEXT("What to do if the asset exists: 'error', 'replace', or 'merge'"),
		{TEXT("error"), TEXT("replace"), TEXT("merge")}));

	TSharedPtr<FJsonObject> SpecRawSchema = MakeShareable(new FJsonObject);
	SpecRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	SpecRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> SpecSchema = MakeShared<FMcpSchemaProperty>();
	SpecSchema->Description = TEXT("Blueprint spec with optional 'variables', 'functions', 'components', and 'event_graph' arrays.");
	SpecSchema->RawSchema = SpecRawSchema;
	SpecSchema->bRequired = true;
	Schema.Add(TEXT("spec"), *SpecSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy: 'never', 'if_needed', or 'always'"),
		{TEXT("never"), TEXT("if_needed"), TEXT("always")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the Blueprint if scaffolding succeeds")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate request shape only")));

	return Schema;
}

TArray<FString> UBlueprintScaffoldFromSpecTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("parent_class"), TEXT("spec")};
}

FMcpToolResult UBlueprintScaffoldFromSpecTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString AssetPath;
	FString ParentClassPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' required"));
	}
	if (!GetStringArg(Arguments, TEXT("parent_class"), ParentClassPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'parent_class' required"));
	}

	const TSharedPtr<FJsonObject>* SpecObject = nullptr;
	if (!Arguments->TryGetObjectField(TEXT("spec"), SpecObject) || !SpecObject || !(*SpecObject).IsValid())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'spec' object required"));
	}

	const FString OverwriteMode = GetStringArgOrDefault(Arguments, TEXT("overwrite_mode"), TEXT("error"));
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("always"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	const bool bAssetExists = FMcpAssetModifier::AssetExists(AssetPath);
	if (bAssetExists && OverwriteMode == TEXT("error"))
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_ASSET_ALREADY_EXISTS"),
			FString::Printf(TEXT("Asset '%s' already exists"), *AssetPath));
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> DryRunResponse = MakeShareable(new FJsonObject);
		DryRunResponse->SetStringField(TEXT("tool"), TEXT("blueprint-scaffold-from-spec"));
		DryRunResponse->SetBoolField(TEXT("success"), true);
		DryRunResponse->SetStringField(TEXT("asset_path"), AssetPath);
		DryRunResponse->SetStringField(TEXT("status"), TEXT("dry_run_validated"));
		return FMcpToolResult::StructuredJson(DryRunResponse);
	}

	TArray<TSharedPtr<FJsonValue>> StepsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	int32 SucceededSteps = 0;
	int32 FailedSteps = 0;

	if (!bAssetExists)
	{
		FString ResolveParentError;
		UClass* ParentClass = FMcpPropertySerializer::ResolveClassOfType<UObject>(ParentClassPath, ResolveParentError);
		if (!ParentClass)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), ResolveParentError);
		}

		const FString AssetName = FPackageName::GetShortName(AssetPath);
		UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			CreatePackage(*AssetPath),
			FName(*AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass());
		if (!NewBlueprint)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), TEXT("Failed to create Blueprint asset"));
		}

		TSharedPtr<FJsonObject> CreateStep = MakeShareable(new FJsonObject);
		CreateStep->SetStringField(TEXT("step"), TEXT("create_asset"));
		CreateStep->SetBoolField(TEXT("success"), true);
		CreateStep->SetStringField(TEXT("asset_path"), AssetPath);
		CreateStep->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
		StepsArray.Add(MakeShareable(new FJsonValueObject(CreateStep)));
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(AssetPath)));
		SucceededSteps++;
	}
	else if (OverwriteMode == TEXT("replace"))
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(TEXT("overwrite_mode='replace' currently reuses the existing Blueprint asset and reapplies the spec instead of deleting/recreating it"))));
	}

	const TArray<TSharedPtr<FJsonValue>>* VariablesArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* FunctionsArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ComponentsArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* EventGraphArray = nullptr;
	(*SpecObject)->TryGetArrayField(TEXT("variables"), VariablesArray);
	(*SpecObject)->TryGetArrayField(TEXT("functions"), FunctionsArray);
	(*SpecObject)->TryGetArrayField(TEXT("components"), ComponentsArray);
	(*SpecObject)->TryGetArrayField(TEXT("event_graph"), EventGraphArray);

	TArray<TSharedPtr<FJsonValue>> MemberActions;
	if (VariablesArray)
	{
		for (const TSharedPtr<FJsonValue>& VariableValue : *VariablesArray)
		{
			const TSharedPtr<FJsonObject>* VariableObject = nullptr;
			if (!VariableValue->TryGetObject(VariableObject) || !(*VariableObject).IsValid())
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActionObject = BlueprintScaffoldFromSpecToolPrivate::CloneJsonObject(*VariableObject);
			if (!ActionObject->HasField(TEXT("action")))
			{
				ActionObject->SetStringField(TEXT("action"), TEXT("create_variable"));
			}
			MemberActions.Add(MakeShareable(new FJsonValueObject(ActionObject)));
		}
	}
	if (FunctionsArray)
	{
		for (const TSharedPtr<FJsonValue>& FunctionValue : *FunctionsArray)
		{
			const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
			if (!FunctionValue->TryGetObject(FunctionObject) || !(*FunctionObject).IsValid())
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActionObject = BlueprintScaffoldFromSpecToolPrivate::CloneJsonObject(*FunctionObject);
			if (!ActionObject->HasField(TEXT("action")))
			{
				ActionObject->SetStringField(TEXT("action"), TEXT("create_function"));
			}
			MemberActions.Add(MakeShareable(new FJsonValueObject(ActionObject)));
		}
	}

	FString StepError;
	if (!BlueprintScaffoldFromSpecToolPrivate::InvokeMembersTool(
		AssetPath,
		MemberActions,
		Context,
		StepsArray,
		WarningsArray,
		DiagnosticsArray,
		ModifiedAssetsArray,
		PartialResultsArray,
		StepError))
	{
		bAnyFailed = true;
		FailedSteps++;
	}
	else if (MemberActions.Num() > 0)
	{
		SucceededSteps++;
	}

	if (!bAnyFailed)
	{
		TArray<TSharedPtr<FJsonValue>> ComponentActions;
		if (ComponentsArray)
		{
			for (const TSharedPtr<FJsonValue>& ComponentValue : *ComponentsArray)
			{
				const TSharedPtr<FJsonObject>* ComponentObject = nullptr;
				if (!ComponentValue->TryGetObject(ComponentObject) || !(*ComponentObject).IsValid())
				{
					continue;
				}

				TSharedPtr<FJsonObject> ActionObject = BlueprintScaffoldFromSpecToolPrivate::CloneJsonObject(*ComponentObject);
				if (!ActionObject->HasField(TEXT("action")))
				{
					ActionObject->SetStringField(TEXT("action"), TEXT("add_component"));
				}
				ComponentActions.Add(MakeShareable(new FJsonValueObject(ActionObject)));
			}
		}

		if (!BlueprintScaffoldFromSpecToolPrivate::InvokeComponentsTool(
			AssetPath,
			ComponentActions,
			Context,
			StepsArray,
			WarningsArray,
			DiagnosticsArray,
			ModifiedAssetsArray,
			PartialResultsArray,
			StepError))
		{
			bAnyFailed = true;
			FailedSteps++;
		}
		else if (ComponentActions.Num() > 0)
		{
			SucceededSteps++;
		}
	}

	if (!bAnyFailed)
	{
		const TArray<TSharedPtr<FJsonValue>> EmptyEventGraphSteps;
		if (!BlueprintScaffoldFromSpecToolPrivate::InvokeEventGraphSteps(
			AssetPath,
			EventGraphArray ? *EventGraphArray : EmptyEventGraphSteps,
			Context,
			StepsArray,
			WarningsArray,
			ModifiedAssetsArray,
			PartialResultsArray,
			StepError))
		{
			bAnyFailed = true;
			FailedSteps++;
		}
		else if (EventGraphArray && EventGraphArray->Num() > 0)
		{
			SucceededSteps++;
		}
	}

	if (!bAnyFailed && CompilePolicy != TEXT("never"))
	{
		TSharedPtr<FJsonObject> CompileArgs = MakeShareable(new FJsonObject);
		CompileArgs->SetArrayField(TEXT("asset_paths"), {MakeShareable(new FJsonValueString(AssetPath))});
		CompileArgs->SetBoolField(TEXT("include_diagnostics"), true);

		UCompileAssetsTool* CompileTool = NewObject<UCompileAssetsTool>();
		const FMcpToolResult CompileResult = CompileTool->Execute(CompileArgs, Context);
		const TSharedPtr<FJsonObject> CompilePayload = BlueprintScaffoldFromSpecToolPrivate::ExtractDataPayload(CompileResult);

		TSharedPtr<FJsonObject> CompileStep = MakeShareable(new FJsonObject);
		CompileStep->SetStringField(TEXT("step"), TEXT("compile"));
		CompileStep->SetBoolField(TEXT("success"), CompileResult.bSuccess);
		if (CompilePayload.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ResultsArray = nullptr;
			if (CompilePayload->TryGetArrayField(TEXT("results"), ResultsArray) && ResultsArray)
			{
				CompileStep->SetArrayField(TEXT("results"), *ResultsArray);
			}
		}
		StepsArray.Add(MakeShareable(new FJsonValueObject(CompileStep)));
		BlueprintScaffoldFromSpecToolPrivate::AppendArrayField(CompilePayload, TEXT("diagnostics"), DiagnosticsArray);

		if (!CompileResult.bSuccess)
		{
			bAnyFailed = true;
			FailedSteps++;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(CompileStep)));
		}
		else
		{
			SucceededSteps++;
		}
	}

	if (!bAnyFailed && bSave)
	{
		FString LoadError;
		UObject* Asset = FMcpAssetModifier::LoadAssetByPath(AssetPath, LoadError);
		if (!Asset)
		{
			bAnyFailed = true;
			FailedSteps++;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(BlueprintScaffoldFromSpecToolPrivate::CreateFailedStep(TEXT("save"), LoadError))));
		}
		else
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Asset, false, SaveError))
			{
				bAnyFailed = true;
				FailedSteps++;
				PartialResultsArray.Add(MakeShareable(new FJsonValueObject(BlueprintScaffoldFromSpecToolPrivate::CreateFailedStep(TEXT("save"), SaveError))));
			}
			else
			{
				TSharedPtr<FJsonObject> SaveStep = MakeShareable(new FJsonObject);
				SaveStep->SetStringField(TEXT("step"), TEXT("save"));
				SaveStep->SetBoolField(TEXT("success"), true);
				SaveStep->SetStringField(TEXT("asset_path"), AssetPath);
				StepsArray.Add(MakeShareable(new FJsonValueObject(SaveStep)));
				SucceededSteps++;
			}
		}
	}

	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("total_steps"), StepsArray.Num());
	SummaryObject->SetNumberField(TEXT("succeeded_steps"), SucceededSteps);
	SummaryObject->SetNumberField(TEXT("failed_steps"), FailedSteps);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("blueprint-scaffold-from-spec"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), StepsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), SummaryObject);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
