// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/World/WorldPartitionEditTools.h"

#include "Utils/McpAssetModifier.h"

#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"

namespace
{
	FString CellStateToString(EWorldPartitionRuntimeCellState State)
	{
		switch (State)
		{
		case EWorldPartitionRuntimeCellState::Unloaded:
			return TEXT("unloaded");
		case EWorldPartitionRuntimeCellState::Loaded:
			return TEXT("loaded");
		case EWorldPartitionRuntimeCellState::Activated:
			return TEXT("activated");
		default:
			return TEXT("unknown");
		}
	}

	TSharedPtr<FJsonObject> SerializeStreamingCell(ULevelStreaming* StreamingLevel)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		const UWorldPartitionRuntimeCell* RuntimeCell = StreamingLevel ? Cast<UWorldPartitionRuntimeCell>(StreamingLevel->GetWorldPartitionCell()) : nullptr;
		if (!RuntimeCell || !StreamingLevel)
		{
			return Object;
		}

		Object->SetStringField(TEXT("debug_name"), RuntimeCell->GetDebugName());
		Object->SetStringField(TEXT("guid"), RuntimeCell->GetGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		Object->SetStringField(TEXT("level_package_name"), RuntimeCell->GetLevelPackageName().ToString());
		Object->SetStringField(TEXT("state"), CellStateToString(RuntimeCell->GetCurrentState()));
		Object->SetBoolField(TEXT("should_be_loaded"), StreamingLevel->ShouldBeLoaded());
		Object->SetBoolField(TEXT("should_be_visible"), StreamingLevel->GetShouldBeVisibleFlag());
		Object->SetBoolField(TEXT("loaded"), RuntimeCell->GetCurrentState() != EWorldPartitionRuntimeCellState::Unloaded);
		Object->SetBoolField(TEXT("activated"), RuntimeCell->GetCurrentState() == EWorldPartitionRuntimeCellState::Activated);
		return Object;
	}

	bool MatchesCell(ULevelStreaming* StreamingLevel, const FString& CellId)
	{
		if (!StreamingLevel || CellId.IsEmpty())
		{
			return false;
		}

		const UWorldPartitionRuntimeCell* RuntimeCell = Cast<UWorldPartitionRuntimeCell>(StreamingLevel->GetWorldPartitionCell());
		if (!RuntimeCell)
		{
			return false;
		}

		return RuntimeCell->GetDebugName().Equals(CellId, ESearchCase::IgnoreCase)
			|| RuntimeCell->GetGuid().ToString(EGuidFormats::DigitsWithHyphensLower).Equals(CellId, ESearchCase::IgnoreCase)
			|| RuntimeCell->GetLevelPackageName().ToString().Equals(CellId, ESearchCase::IgnoreCase);
	}
}

FString UEditWorldPartitionCellsTool::GetToolDescription() const
{
	return TEXT("Request load, unload, show, hide, or activate changes on current world partition streaming cells.");
}

TMap<FString, FMcpSchemaProperty> UEditWorldPartitionCellsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("World partition cell operations"), TEXT("object")));
	Schema.Add(TEXT("flush_streaming"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Flush level streaming after applying operations. Default: true.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	return Schema;
}

FMcpToolResult UEditWorldPartitionCellsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bFlushStreaming = GetBoolArgOrDefault(Arguments, TEXT("flush_streaming"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No world available"));
	}
	if (!World->GetWorldPartition())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_PARTITION_DISABLED"), TEXT("World partition is not enabled for the target world"));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bAnyFailed = false;
	bool bChanged = false;

	struct FValidatedWorldPartitionOperation
	{
		FString Action;
		ULevelStreaming* StreamingLevel = nullptr;
		TSharedPtr<FJsonObject> ResultObject;
	};
	TArray<FValidatedWorldPartitionOperation> ValidOperations;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		const FString CellId = GetStringArgOrDefault(*OperationObject, TEXT("cell"));
		const FString Action = GetStringArgOrDefault(*OperationObject, TEXT("action")).ToLower();
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("cell"), CellId);
		ResultObject->SetStringField(TEXT("action"), Action);

		ULevelStreaming* MatchedStreamingLevel = nullptr;
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (MatchesCell(StreamingLevel, CellId))
			{
				MatchedStreamingLevel = StreamingLevel;
				break;
			}
		}

		if (!MatchedStreamingLevel)
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("World partition runtime cell was not found among current streaming levels"));
			bAnyFailed = true;
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			continue;
		}

		const bool bSupportedAction = Action == TEXT("load")
			|| Action == TEXT("unload")
			|| Action == TEXT("show")
			|| Action == TEXT("hide")
			|| Action == TEXT("activate");
		if (!bSupportedAction)
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("Unsupported action; use load, unload, show, hide, or activate"));
			bAnyFailed = true;
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			continue;
		}

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetObjectField(TEXT("cell_state"), SerializeStreamingCell(MatchedStreamingLevel));
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		FValidatedWorldPartitionOperation ValidatedOperation;
		ValidatedOperation.Action = Action;
		ValidatedOperation.StreamingLevel = MatchedStreamingLevel;
		ValidatedOperation.ResultObject = ResultObject;
		ValidOperations.Add(ValidatedOperation);
	}

	if (!bDryRun && !bAnyFailed)
	{
		for (const FValidatedWorldPartitionOperation& Operation : ValidOperations)
		{
			if (Operation.Action == TEXT("load"))
			{
				Operation.StreamingLevel->SetShouldBeLoaded(true);
			}
			else if (Operation.Action == TEXT("unload"))
			{
				Operation.StreamingLevel->SetShouldBeVisible(false);
				Operation.StreamingLevel->SetShouldBeLoaded(false);
			}
			else if (Operation.Action == TEXT("show") || Operation.Action == TEXT("activate"))
			{
				Operation.StreamingLevel->SetShouldBeLoaded(true);
				Operation.StreamingLevel->SetShouldBeVisible(true);
			}
			else if (Operation.Action == TEXT("hide"))
			{
				Operation.StreamingLevel->SetShouldBeVisible(false);
			}

			bChanged = true;
			if (Operation.ResultObject.IsValid())
			{
				Operation.ResultObject->SetObjectField(TEXT("cell_state"), SerializeStreamingCell(Operation.StreamingLevel));
			}
		}
	}

	if (!bDryRun && bChanged && bFlushStreaming)
	{
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("world_name"), World->GetName());
	Response->SetArrayField(TEXT("results"), ResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
