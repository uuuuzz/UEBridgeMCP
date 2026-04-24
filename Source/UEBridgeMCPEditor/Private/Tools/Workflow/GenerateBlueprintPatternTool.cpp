// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Workflow/GenerateBlueprintPatternTool.h"

#include "Tools/Blueprint/CreateBlueprintPatternTool.h"
#include "Utils/McpAssetModifier.h"

namespace
{
	bool IsSupportedBlueprintPattern(const FString& Pattern)
	{
		return Pattern == TEXT("logic_actor_skeleton")
			|| Pattern == TEXT("toggle_state_actor")
			|| Pattern == TEXT("interaction_stub_actor");
	}

	TSharedPtr<FJsonObject> BuildDelegatedArguments(
		const FString& AssetPath,
		const FString& Pattern,
		const FString& CompilePolicy,
		const bool bSave)
	{
		TSharedPtr<FJsonObject> DelegatedArguments = MakeShareable(new FJsonObject);
		DelegatedArguments->SetStringField(TEXT("asset_path"), AssetPath);
		DelegatedArguments->SetStringField(TEXT("pattern"), Pattern);
		DelegatedArguments->SetStringField(TEXT("compile"), CompilePolicy);
		DelegatedArguments->SetBoolField(TEXT("save"), bSave);
		return DelegatedArguments;
	}
}

FString UGenerateBlueprintPatternTool::GetToolDescription() const
{
	return TEXT("Generate a curated Blueprint pattern through the high-level workflow surface. v1 delegates to create-blueprint-pattern and keeps the same safe Actor catalog.");
}

TMap<FString, FMcpSchemaProperty> UGenerateBlueprintPatternTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("New Blueprint asset path"), true));
	Schema.Add(TEXT("pattern"), FMcpSchemaProperty::MakeEnum(
		TEXT("Built-in Actor Blueprint pattern"),
		{
			TEXT("logic_actor_skeleton"),
			TEXT("toggle_state_actor"),
			TEXT("interaction_stub_actor")
		},
		true));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(TEXT("Final compile policy: 'never' or 'final'. Default: final."), {TEXT("never"), TEXT("final")}));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after successful generation. Default: false.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate and return the delegated plan without creating the Blueprint.")));
	return Schema;
}

FMcpToolResult UGenerateBlueprintPatternTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString Pattern = GetStringArgOrDefault(Arguments, TEXT("pattern"));
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("final"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	if (AssetPath.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' is required"));
	}
	if (!IsSupportedBlueprintPattern(Pattern))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), FString::Printf(TEXT("Unsupported Blueprint pattern '%s'"), *Pattern));
	}
	if (!CompilePolicy.Equals(TEXT("never"), ESearchCase::IgnoreCase)
		&& !CompilePolicy.Equals(TEXT("final"), ESearchCase::IgnoreCase))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_SCHEMA"), TEXT("'compile' must be 'never' or 'final'"));
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Asset already exists"), Details);
	}

	TSharedPtr<FJsonObject> DelegatedArguments = BuildDelegatedArguments(AssetPath, Pattern, CompilePolicy, bSave);

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> PlannedCall = MakeShareable(new FJsonObject);
		PlannedCall->SetStringField(TEXT("name"), TEXT("create-blueprint-pattern"));
		PlannedCall->SetObjectField(TEXT("arguments"), DelegatedArguments);

		TArray<TSharedPtr<FJsonValue>> ToolCalls;
		ToolCalls.Add(MakeShareable(new FJsonValueObject(PlannedCall)));

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), GetToolName());
		Response->SetBoolField(TEXT("success"), true);
		Response->SetBoolField(TEXT("dry_run"), true);
		Response->SetStringField(TEXT("asset_path"), AssetPath);
		Response->SetStringField(TEXT("pattern"), Pattern);
		Response->SetStringField(TEXT("delegated_tool"), TEXT("create-blueprint-pattern"));
		Response->SetArrayField(TEXT("tool_calls"), ToolCalls);
		Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
		return FMcpToolResult::StructuredSuccess(Response, TEXT("Blueprint pattern generation plan ready"));
	}

	UCreateBlueprintPatternTool* CreatePatternTool = NewObject<UCreateBlueprintPatternTool>();
	const FMcpToolResult CreateResult = CreatePatternTool->Execute(DelegatedArguments, Context);
	const TSharedPtr<FJsonObject> DelegatedPayload = CreateResult.GetStructuredContent();
	if (!CreateResult.bSuccess)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		Details->SetStringField(TEXT("pattern"), Pattern);
		if (DelegatedPayload.IsValid())
		{
			Details->SetObjectField(TEXT("delegated_response"), DelegatedPayload);
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_GENERATE_BLUEPRINT_PATTERN_FAILED"), TEXT("create-blueprint-pattern failed"), Details);
	}

	TArray<TSharedPtr<FJsonValue>> Steps;
	TSharedPtr<FJsonObject> Step = MakeShareable(new FJsonObject);
	Step->SetStringField(TEXT("tool_name"), TEXT("create-blueprint-pattern"));
	Step->SetBoolField(TEXT("success"), true);
	if (DelegatedPayload.IsValid())
	{
		Step->SetObjectField(TEXT("response"), DelegatedPayload);
	}
	Steps.Add(MakeShareable(new FJsonValueObject(Step)));

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	ModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), false);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("pattern"), Pattern);
	Response->SetStringField(TEXT("delegated_tool"), TEXT("create-blueprint-pattern"));
	Response->SetArrayField(TEXT("steps"), Steps);
	Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());
	return FMcpToolResult::StructuredSuccess(Response, TEXT("Blueprint pattern generated"));
}
