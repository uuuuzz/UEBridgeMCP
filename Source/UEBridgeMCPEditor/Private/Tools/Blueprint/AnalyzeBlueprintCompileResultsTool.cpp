#include "Tools/Blueprint/AnalyzeBlueprintCompileResultsTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Blueprint/BlueprintCompileUtils.h"
#include "Tools/Blueprint/BlueprintFindingUtils.h"
#include "Utils/McpV2ToolUtils.h"
#include "Engine/Blueprint.h"

namespace
{
	void AppendIssueFromObject(
		const TSharedPtr<FJsonObject>& SourceObject,
		const FString& SourceKind,
		TArray<TSharedPtr<FJsonValue>>& OutIssues)
	{
		if (!SourceObject.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> Issue = MakeShareable(new FJsonObject);
		Issue->SetStringField(TEXT("source"), SourceKind);

		static const TArray<FString> CopiedFields = {
			TEXT("severity"),
			TEXT("code"),
			TEXT("message"),
			TEXT("graph_name"),
			TEXT("node_guid")
		};

		for (const FString& FieldName : CopiedFields)
		{
			FString FieldValue;
			if (SourceObject->TryGetStringField(FieldName, FieldValue))
			{
				Issue->SetStringField(FieldName, FieldValue);
			}
		}

		if (const TSharedPtr<FJsonValue>* HandleValue = SourceObject->Values.Find(TEXT("handle")))
		{
			Issue->SetField(TEXT("handle"), *HandleValue);
		}

		OutIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
	}

	void AddSuggestedFixup(const FString& Action, TArray<FString>& InOutOrderedActions)
	{
		if (!Action.IsEmpty() && !InOutOrderedActions.Contains(Action))
		{
			InOutOrderedActions.Add(Action);
		}
	}
}

FString UAnalyzeBlueprintCompileResultsTool::GetToolDescription() const
{
	return TEXT("Compile a Blueprint in memory, collect compile diagnostics plus structural findings, normalize them into issues, and suggest safe fixups.");
}

TMap<FString, FMcpSchemaProperty> UAnalyzeBlueprintCompileResultsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));
	Schema.Add(TEXT("max_diagnostics"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum compile diagnostics to return (default 100)")));
	Schema.Add(TEXT("max_findings"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum structural findings to return (default 100)")));
	return Schema;
}

TArray<FString> UAnalyzeBlueprintCompileResultsTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult UAnalyzeBlueprintCompileResultsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const int32 MaxDiagnostics = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_diagnostics"), 100));
	const int32 MaxFindings = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("max_findings"), 100));

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	BlueprintCompileUtils::FCompileReport CompileReport;
	BlueprintCompileUtils::CompileBlueprintWithReport(Blueprint, AssetPath, Context.SessionId, MaxDiagnostics, CompileReport);

	BlueprintFindingUtils::FQuery FindingQuery;
	FindingQuery.AssetPath = AssetPath;
	FindingQuery.SessionId = Context.SessionId;
	FindingQuery.MaxFindings = MaxFindings;
	TArray<TSharedPtr<FJsonValue>> Findings = BlueprintFindingUtils::CollectFindings(Blueprint, FindingQuery);

	TArray<TSharedPtr<FJsonValue>> Issues;
	TArray<FString> SuggestedFixupActions;

	for (const TSharedPtr<FJsonValue>& DiagnosticValue : CompileReport.Diagnostics)
	{
		const TSharedPtr<FJsonObject>* DiagnosticObject = nullptr;
		if (DiagnosticValue.IsValid() && DiagnosticValue->TryGetObject(DiagnosticObject) && DiagnosticObject && (*DiagnosticObject).IsValid())
		{
			AppendIssueFromObject(*DiagnosticObject, TEXT("compile"), Issues);
		}
	}

	for (const TSharedPtr<FJsonValue>& FindingValue : Findings)
	{
		const TSharedPtr<FJsonObject>* FindingObject = nullptr;
		if (!FindingValue.IsValid() || !FindingValue->TryGetObject(FindingObject) || !FindingObject || !(*FindingObject).IsValid())
		{
			continue;
		}

		AppendIssueFromObject(*FindingObject, TEXT("finding"), Issues);

		FString Code;
		if (!(*FindingObject)->TryGetStringField(TEXT("code"), Code))
		{
			continue;
		}

		if (Code == TEXT("orphan_pin"))
		{
			AddSuggestedFixup(TEXT("remove_orphan_pins"), SuggestedFixupActions);
		}
		else if (Code == TEXT("broken_or_deprecated_reference") || Code == TEXT("unresolved_member_reference"))
		{
			AddSuggestedFixup(TEXT("refresh_all_nodes"), SuggestedFixupActions);
			AddSuggestedFixup(TEXT("reconstruct_invalid_nodes"), SuggestedFixupActions);
		}
		else if (Code == TEXT("missing_interface_graph"))
		{
			AddSuggestedFixup(TEXT("conform_implemented_interfaces"), SuggestedFixupActions);
		}
	}

	if (!CompileReport.bSuccess && SuggestedFixupActions.Num() == 0)
	{
		AddSuggestedFixup(TEXT("refresh_all_nodes"), SuggestedFixupActions);
		AddSuggestedFixup(TEXT("reconstruct_invalid_nodes"), SuggestedFixupActions);
	}

	TArray<TSharedPtr<FJsonValue>> SuggestedFixupsArray;
	for (const FString& Action : SuggestedFixupActions)
	{
		SuggestedFixupsArray.Add(MakeShareable(new FJsonValueString(Action)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("analyze-blueprint-compile-results"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
	Response->SetObjectField(TEXT("compile"), BlueprintCompileUtils::MakeCompileReportJson(CompileReport));
	Response->SetArrayField(TEXT("findings"), Findings);
	Response->SetArrayField(TEXT("issues"), Issues);
	Response->SetNumberField(TEXT("issue_count"), Issues.Num());
	Response->SetArrayField(TEXT("suggested_fixups"), SuggestedFixupsArray);
	return FMcpToolResult::StructuredJson(Response);
}
