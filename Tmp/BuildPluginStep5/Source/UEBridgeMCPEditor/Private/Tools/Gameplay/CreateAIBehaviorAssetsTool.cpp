// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateAIBehaviorAssetsTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardData.h"
#include "ScopedTransaction.h"

namespace
{
	bool ConfigureBlackboardKey(
		UBlackboardData* Blackboard,
		const TSharedPtr<FJsonObject>& KeyObject,
		FString& OutKeyName,
		FString& OutError)
	{
		if (!Blackboard || !KeyObject.IsValid())
		{
			OutError = TEXT("Invalid blackboard key definition");
			return false;
		}

		FString KeyTypeName;
		if (!KeyObject->TryGetStringField(TEXT("name"), OutKeyName) || !KeyObject->TryGetStringField(TEXT("type"), KeyTypeName))
		{
			OutError = TEXT("Each blackboard key requires 'name' and 'type'");
			return false;
		}

		OutKeyName.TrimStartAndEndInline();
		KeyTypeName.TrimStartAndEndInline();
		if (OutKeyName.IsEmpty() || KeyTypeName.IsEmpty())
		{
			OutError = TEXT("Blackboard key name and type cannot be empty");
			return false;
		}

		FName KeyName(*OutKeyName);

		if (KeyTypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_Bool>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_Int>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_Float>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("name"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_Name>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_String>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_Vector>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))
		{
			Blackboard->UpdatePersistentKey<UBlackboardKeyType_Rotator>(KeyName);
		}
		else if (KeyTypeName.Equals(TEXT("object"), ESearchCase::IgnoreCase))
		{
			if (UBlackboardKeyType_Object* KeyType = Blackboard->UpdatePersistentKey<UBlackboardKeyType_Object>(KeyName))
			{
				FString BaseClassPath;
				KeyObject->TryGetStringField(TEXT("base_class"), BaseClassPath);
				if (!BaseClassPath.IsEmpty())
				{
					KeyType->BaseClass = LoadObject<UClass>(nullptr, *BaseClassPath);
				}
			}
		}
		else if (KeyTypeName.Equals(TEXT("class"), ESearchCase::IgnoreCase))
		{
			if (UBlackboardKeyType_Class* KeyType = Blackboard->UpdatePersistentKey<UBlackboardKeyType_Class>(KeyName))
			{
				FString BaseClassPath;
				KeyObject->TryGetStringField(TEXT("base_class"), BaseClassPath);
				if (!BaseClassPath.IsEmpty())
				{
					KeyType->BaseClass = LoadObject<UClass>(nullptr, *BaseClassPath);
				}
			}
		}
		else if (KeyTypeName.Equals(TEXT("enum"), ESearchCase::IgnoreCase))
		{
			UBlackboardKeyType_Enum* KeyType = Blackboard->UpdatePersistentKey<UBlackboardKeyType_Enum>(KeyName);
			if (!KeyType)
			{
				OutError = TEXT("Failed to create enum blackboard key");
				return false;
			}

			FString EnumPath;
			KeyObject->TryGetStringField(TEXT("enum_path"), EnumPath);
			if (EnumPath.IsEmpty())
			{
				OutError = TEXT("'enum_path' is required for enum blackboard keys");
				return false;
			}

			KeyType->EnumType = FindFirstObject<UEnum>(*EnumPath, EFindFirstObjectOptions::None);
			if (!KeyType->EnumType)
			{
				KeyType->EnumType = LoadObject<UEnum>(nullptr, *EnumPath);
			}
			if (!KeyType->EnumType)
			{
				OutError = FString::Printf(TEXT("Failed to load enum '%s'"), *EnumPath);
				return false;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported blackboard key type '%s'"), *KeyTypeName);
			return false;
		}

		if (const int32 KeyIndex = Blackboard->Keys.IndexOfByPredicate([KeyName](const FBlackboardEntry& Entry)
			{
				return Entry.EntryName == KeyName;
			}); KeyIndex != INDEX_NONE)
		{
			bool bInstanceSynced = false;
			if (KeyObject->TryGetBoolField(TEXT("instance_synced"), bInstanceSynced))
			{
				Blackboard->Keys[KeyIndex].bInstanceSynced = bInstanceSynced;
			}
		}

		return true;
	}
}

FString UCreateAIBehaviorAssetsTool::GetToolDescription() const
{
	return TEXT("Create coordinated Blackboard and BehaviorTree assets for AI authoring.");
}

TMap<FString, FMcpSchemaProperty> UCreateAIBehaviorAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("folder_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination folder path"), true));
	Schema.Add(TEXT("base_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Shared base name for AI assets"), true));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save created assets")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));

	TSharedPtr<FMcpSchemaProperty> KeySchema = MakeShared<FMcpSchemaProperty>();
	KeySchema->Type = TEXT("object");
	KeySchema->Description = TEXT("Blackboard key descriptor");
	KeySchema->NestedRequired = { TEXT("name"), TEXT("type") };
	KeySchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blackboard key name"), true)));
	KeySchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Blackboard key type"),
		{ TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("name"), TEXT("string"), TEXT("vector"), TEXT("rotator"), TEXT("object"), TEXT("class"), TEXT("enum") },
		true)));
	KeySchema->Properties.Add(TEXT("base_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Base class path for object/class keys"))));
	KeySchema->Properties.Add(TEXT("enum_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Enum path for enum keys"))));
	KeySchema->Properties.Add(TEXT("instance_synced"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Whether this key is instance synced"))));

	FMcpSchemaProperty KeysSchema;
	KeysSchema.Type = TEXT("array");
	KeysSchema.Description = TEXT("Optional blackboard key descriptors");
	KeysSchema.Items = KeySchema;
	Schema.Add(TEXT("blackboard_keys"), KeysSchema);
	return Schema;
}

TArray<FString> UCreateAIBehaviorAssetsTool::GetRequiredParams() const
{
	return { TEXT("folder_path"), TEXT("base_name") };
}

FMcpToolResult UCreateAIBehaviorAssetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString FolderPath = GetStringArgOrDefault(Arguments, TEXT("folder_path"));
	const FString BaseName = GetStringArgOrDefault(Arguments, TEXT("base_name"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	if (FolderPath.IsEmpty() || !FolderPath.StartsWith(TEXT("/")))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), TEXT("'folder_path' must be a valid Unreal folder path"));
	}
	if (BaseName.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'base_name' is required"));
	}

	const FString BlackboardPath = FString::Printf(TEXT("%s/BB_%s"), *FolderPath, *BaseName);
	const FString BehaviorTreePath = FString::Printf(TEXT("%s/BT_%s"), *FolderPath, *BaseName);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(BlackboardPath, ValidateError) || !FMcpAssetModifier::ValidateAssetPath(BehaviorTreePath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(BlackboardPath) || FMcpAssetModifier::AssetExists(BehaviorTreePath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("One or more target AI assets already exist"));
	}

	const TArray<TSharedPtr<FJsonValue>>* BlackboardKeys = nullptr;
	Arguments->TryGetArrayField(TEXT("blackboard_keys"), BlackboardKeys);

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> DryRunResponse = MakeShareable(new FJsonObject);
		DryRunResponse->SetStringField(TEXT("tool"), GetToolName());
		DryRunResponse->SetBoolField(TEXT("success"), true);
		DryRunResponse->SetBoolField(TEXT("dry_run"), true);
		DryRunResponse->SetStringField(TEXT("blackboard_asset_path"), BlackboardPath);
		DryRunResponse->SetStringField(TEXT("behavior_tree_asset_path"), BehaviorTreePath);
		DryRunResponse->SetNumberField(TEXT("blackboard_key_count"), BlackboardKeys ? BlackboardKeys->Num() : 0);
		return FMcpToolResult::StructuredJson(DryRunResponse);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create AI Behavior Assets")));

	FString BlackboardError;
	UBlackboardData* Blackboard = Cast<UBlackboardData>(GameplayToolUtils::CreateObjectAsset(UBlackboardData::StaticClass(), BlackboardPath, BlackboardError));
	if (!Blackboard)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), BlackboardError);
	}

	if (BlackboardKeys)
	{
		for (int32 KeyIndex = 0; KeyIndex < BlackboardKeys->Num(); ++KeyIndex)
		{
			const TSharedPtr<FJsonObject>* KeyObject = nullptr;
			if (!(*BlackboardKeys)[KeyIndex].IsValid() || !(*BlackboardKeys)[KeyIndex]->TryGetObject(KeyObject) || !KeyObject || !(*KeyObject).IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("blackboard_keys[%d] must be an object"), KeyIndex));
			}

			FString KeyName;
			FString KeyError;
			if (!ConfigureBlackboardKey(Blackboard, *KeyObject, KeyName, KeyError))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("blackboard_keys[%d]: %s"), KeyIndex, *KeyError));
			}
		}
	}

	Blackboard->UpdateParentKeys();
	Blackboard->UpdateKeyIDs();
	Blackboard->UpdateIfHasSynchronizedKeys();
	FMcpAssetModifier::MarkPackageDirty(Blackboard);

	FString BehaviorTreeError;
	UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(GameplayToolUtils::CreateObjectAsset(UBehaviorTree::StaticClass(), BehaviorTreePath, BehaviorTreeError));
	if (!BehaviorTree)
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), BehaviorTreeError);
	}

	BehaviorTree->Modify();
	BehaviorTree->BlackboardAsset = Blackboard;
	FMcpAssetModifier::MarkPackageDirty(BehaviorTree);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Blackboard, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
		if (!FMcpAssetModifier::SaveAsset(BehaviorTree, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TArray<TSharedPtr<FJsonValue>> KeyResults;
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		TSharedPtr<FJsonObject> EntryObject = MakeShareable(new FJsonObject);
		EntryObject->SetStringField(TEXT("name"), Entry.EntryName.ToString());
		EntryObject->SetStringField(TEXT("type"), Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : TEXT("Unknown"));
		EntryObject->SetBoolField(TEXT("instance_synced"), Entry.bInstanceSynced);
		KeyResults.Add(MakeShareable(new FJsonValueObject(EntryObject)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("blackboard_asset_path"), BlackboardPath);
	Response->SetStringField(TEXT("behavior_tree_asset_path"), BehaviorTreePath);
	Response->SetArrayField(TEXT("blackboard_keys"), KeyResults);
	return FMcpToolResult::StructuredJson(Response);
}
