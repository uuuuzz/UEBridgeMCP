// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Landscape/QueryLandscapeSummaryTool.h"

#include "Tools/Landscape/LandscapeToolUtils.h"
#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "EngineUtils.h"
#include "Landscape.h"

FString UQueryLandscapeSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize Landscape actors in an editor or PIE world, including bounds, components, layers, and optional height samples.");
}

TMap<FString, FMcpSchemaProperty> UQueryLandscapeSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to inspect"), { TEXT("editor"), TEXT("pie") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional Landscape actor label or name filter")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional Landscape actor handle")));
	Schema.Add(TEXT("include_components"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include per-component summaries. Default: false")));
	Schema.Add(TEXT("max_components"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum components to include when include_components=true. Default: 16")));
	Schema.Add(TEXT("sample_points"), FMcpSchemaProperty::Make(TEXT("array"), TEXT("Optional landscape-coordinate sample points as arrays like [[x,y], [x,y]]")));
	return Schema;
}

FMcpToolResult UQueryLandscapeSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), false);
	const int32 MaxComponents = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_components"), 16));
	const TArray<FIntPoint> SamplePoints = LandscapeToolUtils::ReadSamplePoints(Arguments);

	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the requested world"));
	}

	TArray<ALandscapeProxy*> LandscapeProxies;
	if (Arguments->HasField(TEXT("actor_name")) || Arguments->HasField(TEXT("actor_handle")))
	{
		UWorld* ResolvedWorld = nullptr;
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ErrorDetails;
		AActor* Actor = LevelActorToolUtils::ResolveActorReference(
			Arguments,
			RequestedWorldType,
			TEXT("actor_name"),
			TEXT("actor_handle"),
			Context,
			ResolvedWorld,
			ErrorCode,
			ErrorMessage,
			ErrorDetails,
			true);

		ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(Actor);
		if (!LandscapeProxy)
		{
			return FMcpToolResult::StructuredError(
				TEXT("UEBMCP_LANDSCAPE_NOT_FOUND"),
				Actor ? TEXT("Target actor is not a LandscapeProxy") : ErrorMessage,
				ErrorDetails);
		}

		World = ResolvedWorld ? ResolvedWorld : World;
		LandscapeProxies.Add(LandscapeProxy);
	}
	else
	{
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			if (ALandscapeProxy* LandscapeProxy = *It)
			{
				LandscapeProxies.Add(LandscapeProxy);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> LandscapesArray;
	int32 TotalComponentCount = 0;
	for (ALandscapeProxy* LandscapeProxy : LandscapeProxies)
	{
		if (!LandscapeProxy)
		{
			continue;
		}

		TArray<ULandscapeComponent*> Components;
		LandscapeProxy->GetComponents<ULandscapeComponent>(Components);
		TotalComponentCount += Components.Num();
		LandscapesArray.Add(MakeShareable(new FJsonValueObject(
			LandscapeToolUtils::BuildLandscapeSummary(LandscapeProxy, Context.SessionId, bIncludeComponents, MaxComponents, SamplePoints))));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("world_path"), World->GetPathName());
	Result->SetStringField(TEXT("world_type"), RequestedWorldType);
	Result->SetNumberField(TEXT("landscape_count"), LandscapesArray.Num());
	Result->SetNumberField(TEXT("component_count"), TotalComponentCount);
	Result->SetArrayField(TEXT("landscapes"), LandscapesArray);

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Landscape summary ready"));
}
