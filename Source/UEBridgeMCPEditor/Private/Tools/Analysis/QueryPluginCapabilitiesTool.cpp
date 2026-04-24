// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Analysis/QueryPluginCapabilitiesTool.h"

#include "Tools/Analysis/EngineApiToolUtils.h"
#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"
#include "Interfaces/IPluginManager.h"

FString UQueryPluginCapabilitiesTool::GetToolDescription() const
{
	return TEXT("Query local plugin capability metadata: enabled/mounted state, content support, module descriptors, and selected descriptor fields.");
}

TMap<FString, FMcpSchemaProperty> UQueryPluginCapabilitiesTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("plugin_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional exact plugin name to inspect.")));
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional ranked query across plugin name, friendly name, description, and category.")));
	Schema.Add(TEXT("enabled_only"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Only return enabled plugins. Default: false.")));
	Schema.Add(TEXT("include_modules"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include plugin module descriptors. Default: true.")));
	Schema.Add(TEXT("include_paths"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include local filesystem paths. Default: false.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum plugin results. Default: 50, max: 500.")));
	return Schema;
}

FMcpToolResult UQueryPluginCapabilitiesTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString PluginName = GetStringArgOrDefault(Arguments, TEXT("plugin_name"));
	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"), PluginName);
	const bool bEnabledOnly = GetBoolArgOrDefault(Arguments, TEXT("enabled_only"), false);
	const bool bIncludeModules = GetBoolArgOrDefault(Arguments, TEXT("include_modules"), true);
	const bool bIncludePaths = GetBoolArgOrDefault(Arguments, TEXT("include_paths"), false);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 50), 1, 500);

	TArray<EngineApiToolUtils::FScoredJson> Items;
	int32 Scanned = 0;
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		++Scanned;
		if (bEnabledOnly && !Plugin->IsEnabled())
		{
			continue;
		}
		if (!PluginName.IsEmpty() && !Plugin->GetName().Equals(PluginName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		const double Score = SearchToolUtils::ScoreFields({ Plugin->GetName(), Plugin->GetFriendlyName(), Descriptor.Description, Descriptor.Category, Descriptor.CreatedBy }, Query);
		if (!Query.IsEmpty() && Score <= 0.0)
		{
			continue;
		}
		Items.Add({ Score, EngineApiToolUtils::SerializePlugin(Plugin, bIncludeModules, bIncludePaths, Score) });
	}

	if (!PluginName.IsEmpty() && Items.Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_PLUGIN_NOT_FOUND"), FString::Printf(TEXT("Plugin '%s' not found"), *PluginName));
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	EngineApiToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetArrayField(TEXT("plugins"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetNumberField(TEXT("plugins_scanned"), Scanned);
	Result->SetBoolField(TEXT("enabled_only"), bEnabledOnly);
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Plugin capabilities ready"));
}
