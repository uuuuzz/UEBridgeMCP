// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Niagara/QueryNiagaraEmitterSummaryTool.h"

#include "Tools/Niagara/NiagaraToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"

namespace
{
	TSharedPtr<FJsonObject> SerializeEmitterAsset(UNiagaraEmitter* Emitter)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		if (!Emitter)
		{
			return Result;
		}

		Result->SetStringField(TEXT("asset_path"), Emitter->GetPathName());
		Result->SetStringField(TEXT("name"), Emitter->GetName());
		Result->SetStringField(TEXT("class"), Emitter->GetClass()->GetName());
		return Result;
	}
}

FString UQueryNiagaraEmitterSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize a Niagara emitter asset, or an emitter handle inside a Niagara system by system_path and emitter_name.");
}

TMap<FString, FMcpSchemaProperty> UQueryNiagaraEmitterSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Niagara emitter asset path")));
	Schema.Add(TEXT("system_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Niagara system asset path when querying an emitter handle")));
	Schema.Add(TEXT("emitter_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Emitter handle display name inside the system")));
	Schema.Add(TEXT("include_renderers"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include enabled renderer summaries")));
	return Schema;
}

FMcpToolResult UQueryNiagaraEmitterSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString SystemPath = GetStringArgOrDefault(Arguments, TEXT("system_path"));
	const FString EmitterName = GetStringArgOrDefault(Arguments, TEXT("emitter_name"));
	const bool bIncludeRenderers = GetBoolArgOrDefault(Arguments, TEXT("include_renderers"), true);

	if (!AssetPath.IsEmpty())
	{
		FString LoadError;
		UNiagaraEmitter* Emitter = FMcpAssetModifier::LoadAssetByPath<UNiagaraEmitter>(AssetPath, LoadError);
		if (!Emitter)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}

		TSharedPtr<FJsonObject> Result = SerializeEmitterAsset(Emitter);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetStringField(TEXT("source"), TEXT("asset"));
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Niagara emitter summary ready"));
	}

	if (SystemPath.IsEmpty() || EmitterName.IsEmpty())
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_MISSING_REQUIRED_FIELD"),
			TEXT("Provide 'asset_path', or both 'system_path' and 'emitter_name'"));
	}

	FString LoadError;
	UNiagaraSystem* System = FMcpAssetModifier::LoadAssetByPath<UNiagaraSystem>(SystemPath, LoadError);
	if (!System)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Result = NiagaraToolUtils::SerializeEmitterHandle(Handle, bIncludeRenderers);
			Result->SetStringField(TEXT("tool"), GetToolName());
			Result->SetStringField(TEXT("source"), TEXT("system"));
			Result->SetStringField(TEXT("system_path"), System->GetPathName());
			return FMcpToolResult::StructuredSuccess(Result, TEXT("Niagara emitter handle summary ready"));
		}
	}

	TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
	Details->SetStringField(TEXT("system_path"), SystemPath);
	Details->SetStringField(TEXT("emitter_name"), EmitterName);
	return FMcpToolResult::StructuredError(TEXT("UEBMCP_NIAGARA_EMITTER_NOT_FOUND"), TEXT("Niagara emitter handle not found"), Details);
}
