// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/ManageGameplayTagsTool.h"

#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"

namespace
{
	int32 FindTagRowIndex(const TArray<FGameplayTagTableRow>& Rows, const FName TagName)
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			if (Rows[Index].Tag == TagName)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

FString UManageGameplayTagsTool::GetToolDescription() const
{
	return TEXT("Manage project gameplay tags and replication-related gameplay tag settings.");
}

TMap<FString, FMcpSchemaProperty> UManageGameplayTagsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Gameplay tag settings operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Gameplay tag operation"),
		{ TEXT("add_tag"), TEXT("remove_tag"), TEXT("set_fast_replication"), TEXT("set_commonly_replicated_tags") },
		true)));
	OperationSchema->Properties.Add(TEXT("tag"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Gameplay tag name"))));
	OperationSchema->Properties.Add(TEXT("comment"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Developer comment for add_tag"))));
	OperationSchema->Properties.Add(TEXT("enabled"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Fast replication enabled flag"))));
	OperationSchema->Properties.Add(TEXT("tags"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Gameplay tag names"), TEXT("string"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Gameplay tag operations");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Persist settings to config")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UManageGameplayTagsTool::GetRequiredParams() const
{
	return { TEXT("operations") };
}

FMcpToolResult UManageGameplayTagsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	UGameplayTagsSettings* TagSettings = GetMutableDefault<UGameplayTagsSettings>();
	if (!TagSettings)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_SETTINGS_UNAVAILABLE"), TEXT("GameplayTagsSettings is not available"));
	}

	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	TArray<FGameplayTagTableRow> UpdatedTags = TagSettings->GameplayTagList;
	TArray<FName> UpdatedCommonTags = TagSettings->CommonlyReplicatedTags;
	bool bFastReplication = TagSettings->FastReplication;

	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString ActionName;
		(*OperationObject)->TryGetStringField(TEXT("action"), ActionName);
		ResultObject->SetStringField(TEXT("action"), ActionName);

		bool bChanged = false;

		if (ActionName == TEXT("add_tag"))
		{
			FString TagString;
			if (!(*OperationObject)->TryGetStringField(TEXT("tag"), TagString) || TagString.IsEmpty())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("operations[%d].tag is required"), OperationIndex));
			}

			FText ValidationError;
			FString FixedTagString;
			if (!FGameplayTag::IsValidGameplayTagString(TagString, &ValidationError, &FixedTagString))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ValidationError.ToString());
			}
			if (!FixedTagString.IsEmpty())
			{
				TagString = FixedTagString;
			}

			const FName TagName(*TagString);
			const int32 ExistingIndex = FindTagRowIndex(UpdatedTags, TagName);
			if (ExistingIndex == INDEX_NONE)
			{
				UpdatedTags.Add(FGameplayTagTableRow(TagName, GetStringArgOrDefault(*OperationObject, TEXT("comment"))));
				bChanged = true;
			}
			else
			{
				const FString NewComment = GetStringArgOrDefault(*OperationObject, TEXT("comment"));
				if (!NewComment.IsEmpty() && UpdatedTags[ExistingIndex].DevComment != NewComment)
				{
					UpdatedTags[ExistingIndex].DevComment = NewComment;
					bChanged = true;
				}
			}
			ResultObject->SetStringField(TEXT("tag"), TagString);
		}
		else if (ActionName == TEXT("remove_tag"))
		{
			FString TagString;
			if (!(*OperationObject)->TryGetStringField(TEXT("tag"), TagString) || TagString.IsEmpty())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("operations[%d].tag is required"), OperationIndex));
			}

			const int32 ExistingIndex = FindTagRowIndex(UpdatedTags, FName(*TagString));
			if (ExistingIndex != INDEX_NONE)
			{
				UpdatedTags.RemoveAt(ExistingIndex);
				bChanged = true;
			}
			ResultObject->SetStringField(TEXT("tag"), TagString);
		}
		else if (ActionName == TEXT("set_fast_replication"))
		{
			bool bEnabled = false;
			if (!(*OperationObject)->TryGetBoolField(TEXT("enabled"), bEnabled))
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("operations[%d].enabled is required"), OperationIndex));
			}

			bChanged = bFastReplication != bEnabled;
			bFastReplication = bEnabled;
			ResultObject->SetBoolField(TEXT("enabled"), bEnabled);
		}
		else if (ActionName == TEXT("set_commonly_replicated_tags"))
		{
			const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
			if (!(*OperationObject)->TryGetArrayField(TEXT("tags"), TagsArray) || !TagsArray)
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("operations[%d].tags is required"), OperationIndex));
			}

			TArray<FName> RequestedTags;
			TArray<TSharedPtr<FJsonValue>> TagValues;
			for (const TSharedPtr<FJsonValue>& TagValue : *TagsArray)
			{
				const FString TagString = TagValue.IsValid() ? TagValue->AsString() : TEXT("");
				if (TagString.IsEmpty())
				{
					continue;
				}
				RequestedTags.AddUnique(FName(*TagString));
				TagValues.Add(MakeShareable(new FJsonValueString(TagString)));
			}

			bChanged = RequestedTags != UpdatedCommonTags;
			UpdatedCommonTags = RequestedTags;
			ResultObject->SetArrayField(TEXT("tags"), TagValues);
		}
		else
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSUPPORTED_ACTION"), FString::Printf(TEXT("Unsupported action '%s'"), *ActionName));
		}

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetBoolField(TEXT("changed"), bChanged);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	UpdatedTags.Sort();
	UpdatedCommonTags.Sort(FNameLexicalLess());

	if (!bDryRun)
	{
		TagSettings->GameplayTagList = UpdatedTags;
		TagSettings->CommonlyReplicatedTags = UpdatedCommonTags;
		TagSettings->FastReplication = bFastReplication;
		TagSettings->SortTags();
		if (bSave)
		{
			TagSettings->SaveConfig();
		}
		UGameplayTagsManager::Get().EditorRefreshGameplayTagTree();
	}

	TArray<TSharedPtr<FJsonValue>> TagRows;
	for (const FGameplayTagTableRow& Row : UpdatedTags)
	{
		TSharedPtr<FJsonObject> TagObject = MakeShareable(new FJsonObject);
		TagObject->SetStringField(TEXT("tag"), Row.Tag.ToString());
		TagObject->SetStringField(TEXT("comment"), Row.DevComment);
		TagRows.Add(MakeShareable(new FJsonValueObject(TagObject)));
	}

	TArray<TSharedPtr<FJsonValue>> CommonTagRows;
	for (const FName& TagName : UpdatedCommonTags)
	{
		CommonTagRows.Add(MakeShareable(new FJsonValueString(TagName.ToString())));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("fast_replication"), bFastReplication);
	Response->SetArrayField(TEXT("gameplay_tags"), TagRows);
	Response->SetArrayField(TEXT("commonly_replicated_tags"), CommonTagRows);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	return FMcpToolResult::StructuredJson(Response);
}
