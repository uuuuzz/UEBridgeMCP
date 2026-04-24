// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryActorDetailTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

FString UQueryActorDetailTool::GetToolDescription() const
{
	return TEXT("Return detailed reflected information for a specific actor in the editor or PIE world.");
}

TMap<FString, FMcpSchemaProperty> UQueryActorDetailTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), {TEXT("editor"), TEXT("pie"), TEXT("auto")}));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Actor name or label")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Actor handle returned by query-level-summary")));
	Schema.Add(TEXT("include_components"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include components")));
	Schema.Add(TEXT("include_properties"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include reflected properties")));
	Schema.Add(TEXT("include_inherited"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include inherited properties")));
	Schema.Add(TEXT("include"), FMcpSchemaProperty::MakeArray(
		TEXT("Optional detail sections: metadata, hierarchy, tags, visibility, mobility, bounds, components, properties"),
		TEXT("string")));
	return Schema;
}

TArray<FString> UQueryActorDetailTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UQueryActorDetailTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const bool bLegacyIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), true);
	const bool bLegacyIncludeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_properties"), false);
	const bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), false);

	TSet<FString> IncludeSections;
	const TArray<TSharedPtr<FJsonValue>>* IncludeArray = nullptr;
	const bool bHasIncludeFilter = Arguments->TryGetArrayField(TEXT("include"), IncludeArray) && IncludeArray;
	if (bHasIncludeFilter)
	{
		for (const TSharedPtr<FJsonValue>& Value : *IncludeArray)
		{
			FString SectionName;
			if (Value.IsValid() && Value->TryGetString(SectionName))
			{
				IncludeSections.Add(SectionName.ToLower());
			}
		}
	}

	const auto ShouldIncludeSection = [&IncludeSections, bHasIncludeFilter](const TCHAR* SectionName, bool bDefaultValue) -> bool
	{
		return bHasIncludeFilter ? IncludeSections.Contains(FString(SectionName).ToLower()) : bDefaultValue;
	};

	const bool bIncludeMetadata = ShouldIncludeSection(TEXT("metadata"), true);
	const bool bIncludeHierarchy = ShouldIncludeSection(TEXT("hierarchy"), true);
	const bool bIncludeTags = ShouldIncludeSection(TEXT("tags"), true);
	const bool bIncludeVisibility = ShouldIncludeSection(TEXT("visibility"), true);
	const bool bIncludeMobility = ShouldIncludeSection(TEXT("mobility"), true);
	const bool bIncludeBounds = ShouldIncludeSection(TEXT("bounds"), true);
	const bool bIncludeComponents = ShouldIncludeSection(TEXT("components"), bLegacyIncludeComponents);
	const bool bIncludeProperties = ShouldIncludeSection(TEXT("properties"), bLegacyIncludeProperties);

	UWorld* World = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	AActor* Actor = LevelActorToolUtils::ResolveActorReference(
		Arguments,
		WorldType,
		TEXT("actor_name"),
		TEXT("actor_handle"),
		Context,
		World,
		ErrorCode,
		ErrorMessage,
		ErrorDetails,
		true);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	TSharedPtr<FJsonObject> Result = McpV2ToolUtils::SerializeActorDetail(
		Actor,
		Context.SessionId,
		bIncludeComponents,
		bIncludeProperties,
		bIncludeInherited,
		bIncludeHierarchy,
		bIncludeTags,
		bIncludeVisibility,
		bIncludeMobility,
		bIncludeBounds);
	if (!Result.IsValid())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INTERNAL_ERROR"), TEXT("Failed to serialize actor detail"));
	}

	if (!bIncludeMetadata)
	{
		Result->RemoveField(TEXT("folder_path"));
		Result->RemoveField(TEXT("level_path"));
	}

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Actor detail ready"));
}
