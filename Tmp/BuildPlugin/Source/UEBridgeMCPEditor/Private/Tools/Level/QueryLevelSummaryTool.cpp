// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryLevelSummaryTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "EngineUtils.h"

FString UQueryLevelSummaryTool::GetToolDescription() const
{
	return TEXT("Return a compact list of actors in the current level or world, with optional filtering.");
}

TMap<FString, FMcpSchemaProperty> UQueryLevelSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), {TEXT("editor"), TEXT("pie"), TEXT("auto")}));
	Schema.Add(TEXT("class_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor class filter")));
	Schema.Add(TEXT("folder_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional World Outliner folder filter")));
	Schema.Add(TEXT("tag_filter"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional actor tag filter")));
	Schema.Add(TEXT("include_hidden"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include hidden actors")));
	Schema.Add(TEXT("include_transform"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actor transforms")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum actors to return")));
	return Schema;
}

FMcpToolResult UQueryLevelSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"));
	const FString FolderFilter = GetStringArgOrDefault(Arguments, TEXT("folder_filter"));
	const FString TagFilter = GetStringArgOrDefault(Arguments, TEXT("tag_filter"));
	const bool bIncludeHidden = GetBoolArgOrDefault(Arguments, TEXT("include_hidden"), false);
	const bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
	const int32 Limit = GetIntArgOrDefault(Arguments, TEXT("limit"), 100);

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}

	const FString WorldIdentifier = World->GetPathName();
	TArray<TSharedPtr<FJsonValue>> ActorArray;
	int32 TotalMatches = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (!bIncludeHidden && Actor->IsHiddenEd())
		{
			continue;
		}
		if (!ClassFilter.IsEmpty() && !McpV2ToolUtils::MatchesPattern(Actor->GetClass()->GetName(), ClassFilter) && !McpV2ToolUtils::MatchesPattern(Actor->GetClass()->GetPathName(), ClassFilter))
		{
			continue;
		}
		if (!FolderFilter.IsEmpty() && !McpV2ToolUtils::MatchesPattern(Actor->GetFolderPath().ToString(), FolderFilter))
		{
			continue;
		}
		if (!TagFilter.IsEmpty())
		{
			bool bTagMatched = false;
			for (const FName& Tag : Actor->Tags)
			{
				if (McpV2ToolUtils::MatchesPattern(Tag.ToString(), TagFilter))
				{
					bTagMatched = true;
					break;
				}
			}
			if (!bTagMatched)
			{
				continue;
			}
		}

		FMcpEditorSessionManager::Get().RememberActor(Context.SessionId, WorldIdentifier, Actor);
		++TotalMatches;
		if (ActorArray.Num() < Limit)
		{
			ActorArray.Add(MakeShareable(new FJsonValueObject(McpV2ToolUtils::SerializeActorSummary(Actor, Context.SessionId, bIncludeTransform))));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("world_path"), WorldIdentifier);
	Result->SetArrayField(TEXT("actors"), ActorArray);
	Result->SetNumberField(TEXT("returned_count"), ActorArray.Num());
	Result->SetNumberField(TEXT("total_matches"), TotalMatches);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Level summary ready"));
}
