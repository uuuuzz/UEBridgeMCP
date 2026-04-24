// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/MetaSound/QueryMetaSoundSummaryTool.h"

#include "Tools/MetaSound/MetaSoundToolUtils.h"

#include "MetasoundSource.h"

FString UQueryMetaSoundSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize a MetaSound Source asset, including graph inputs, outputs, interfaces, and optional default graph nodes/edges.");
}

TMap<FString, FMcpSchemaProperty> UQueryMetaSoundSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("MetaSound Source asset path"), true));
	Schema.Add(TEXT("include_graph"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include default graph nodes and edges")));
	return Schema;
}

FMcpToolResult UQueryMetaSoundSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeGraph = GetBoolArgOrDefault(Arguments, TEXT("include_graph"), false);

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	if (!MetaSoundToolUtils::TryLoadSource(AssetPath, Source, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FJsonObject> Result = MetaSoundToolUtils::SerializeSourceSummary(Source, bIncludeGraph);
	Result->SetStringField(TEXT("tool"), GetToolName());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("MetaSound summary ready"));
}
