// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Niagara/QueryNiagaraSystemSummaryTool.h"

#include "Tools/Niagara/NiagaraToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "NiagaraSystem.h"

FString UQueryNiagaraSystemSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize a Niagara system asset including emitter handles, enabled renderers, readiness, and exposed user parameters.");
}

TMap<FString, FMcpSchemaProperty> UQueryNiagaraSystemSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Niagara system asset path"), true));
	Schema.Add(TEXT("include_emitters"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include emitter handle summaries")));
	Schema.Add(TEXT("include_user_parameters"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include exposed user parameters")));
	return Schema;
}

FMcpToolResult UQueryNiagaraSystemSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeEmitters = GetBoolArgOrDefault(Arguments, TEXT("include_emitters"), true);
	const bool bIncludeUserParameters = GetBoolArgOrDefault(Arguments, TEXT("include_user_parameters"), true);

	FString LoadError;
	UNiagaraSystem* System = FMcpAssetModifier::LoadAssetByPath<UNiagaraSystem>(AssetPath, LoadError);
	if (!System)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FJsonObject> Result = NiagaraToolUtils::SerializeSystemSummary(System, bIncludeEmitters, bIncludeUserParameters);
	Result->SetStringField(TEXT("tool"), GetToolName());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Niagara system summary ready"));
}
