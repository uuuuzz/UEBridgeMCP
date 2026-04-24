// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateGameFrameworkBlueprintSetTool.h"

#include "Tools/Gameplay/GameplayToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "ScopedTransaction.h"

namespace
{
	struct FFrameworkRoleInfo
	{
		FString RoleName;
		UClass* ParentClass = nullptr;
		FString Suffix;
	};

	bool ResolveFrameworkRole(const FString& RoleName, FFrameworkRoleInfo& OutInfo)
	{
		if (RoleName.Equals(TEXT("game_mode"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("game_mode"), AGameModeBase::StaticClass(), TEXT("GameMode") };
			return true;
		}
		if (RoleName.Equals(TEXT("game_state"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("game_state"), AGameStateBase::StaticClass(), TEXT("GameState") };
			return true;
		}
		if (RoleName.Equals(TEXT("player_controller"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("player_controller"), APlayerController::StaticClass(), TEXT("PlayerController") };
			return true;
		}
		if (RoleName.Equals(TEXT("player_state"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("player_state"), APlayerState::StaticClass(), TEXT("PlayerState") };
			return true;
		}
		if (RoleName.Equals(TEXT("pawn"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("pawn"), APawn::StaticClass(), TEXT("Pawn") };
			return true;
		}
		if (RoleName.Equals(TEXT("character"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("character"), ACharacter::StaticClass(), TEXT("Character") };
			return true;
		}
		if (RoleName.Equals(TEXT("hud"), ESearchCase::IgnoreCase))
		{
			OutInfo = { TEXT("hud"), AHUD::StaticClass(), TEXT("HUD") };
			return true;
		}
		return false;
	}
}

FString UCreateGameFrameworkBlueprintSetTool::GetToolDescription() const
{
	return TEXT("Create a coordinated set of GameFramework Blueprints under a shared folder and naming prefix.");
}

TMap<FString, FMcpSchemaProperty> UCreateGameFrameworkBlueprintSetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("folder_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination folder path, e.g. '/Game/Blueprints/GameFramework'"), true));
	Schema.Add(TEXT("base_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Shared base name used for generated assets"), true));
	Schema.Add(TEXT("prefix"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional asset prefix, default 'BP_'")));
	Schema.Add(TEXT("roles"), FMcpSchemaProperty::MakeArray(TEXT("Framework roles to generate"), TEXT("string")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save created Blueprints")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UCreateGameFrameworkBlueprintSetTool::GetRequiredParams() const
{
	return { TEXT("folder_path"), TEXT("base_name") };
}

FMcpToolResult UCreateGameFrameworkBlueprintSetTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString FolderPath = GetStringArgOrDefault(Arguments, TEXT("folder_path"));
	const FString BaseName = GetStringArgOrDefault(Arguments, TEXT("base_name"));
	const FString Prefix = GetStringArgOrDefault(Arguments, TEXT("prefix"), TEXT("BP_"));
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

	TArray<FString> RequestedRoles;
	const TArray<TSharedPtr<FJsonValue>>* RolesArray = nullptr;
	if (Arguments->TryGetArrayField(TEXT("roles"), RolesArray) && RolesArray && RolesArray->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RolesArray)
		{
			if (Value.IsValid())
			{
				RequestedRoles.Add(Value->AsString());
			}
		}
	}
	else
	{
		RequestedRoles = {
			TEXT("game_mode"),
			TEXT("game_state"),
			TEXT("player_controller"),
			TEXT("player_state"),
			TEXT("character")
		};
	}

	TArray<FFrameworkRoleInfo> RolesToCreate;
	for (const FString& RoleName : RequestedRoles)
	{
		FFrameworkRoleInfo RoleInfo;
		if (!ResolveFrameworkRole(RoleName, RoleInfo))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("Unsupported framework role '%s'"), *RoleName));
		}
		RolesToCreate.Add(RoleInfo);
	}

	TArray<TSharedPtr<FJsonValue>> CreatedAssets;
	for (const FFrameworkRoleInfo& RoleInfo : RolesToCreate)
	{
		const FString AssetPath = FString::Printf(TEXT("%s/%s%s%s"), *FolderPath, *Prefix, *BaseName, *RoleInfo.Suffix);
		FString ValidateError;
		if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
		}
		if (FMcpAssetModifier::AssetExists(AssetPath))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
		}

		TSharedPtr<FJsonObject> AssetObject = MakeShareable(new FJsonObject);
		AssetObject->SetStringField(TEXT("role"), RoleInfo.RoleName);
		AssetObject->SetStringField(TEXT("asset_path"), AssetPath);
		AssetObject->SetStringField(TEXT("parent_class"), RoleInfo.ParentClass->GetPathName());
		CreatedAssets.Add(MakeShareable(new FJsonValueObject(AssetObject)));
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> DryRunResponse = MakeShareable(new FJsonObject);
		DryRunResponse->SetStringField(TEXT("tool"), GetToolName());
		DryRunResponse->SetBoolField(TEXT("success"), true);
		DryRunResponse->SetBoolField(TEXT("dry_run"), true);
		DryRunResponse->SetArrayField(TEXT("created_assets"), CreatedAssets);
		return FMcpToolResult::StructuredJson(DryRunResponse);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create GameFramework Blueprint Set")));
	for (const FFrameworkRoleInfo& RoleInfo : RolesToCreate)
	{
		const FString AssetPath = FString::Printf(TEXT("%s/%s%s%s"), *FolderPath, *Prefix, *BaseName, *RoleInfo.Suffix);
		FString CreateError;
		UBlueprint* Blueprint = GameplayToolUtils::CreateBlueprintAsset(AssetPath, RoleInfo.ParentClass, CreateError);
		if (!Blueprint)
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), CreateError);
		}

		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetArrayField(TEXT("created_assets"), CreatedAssets);
	return FMcpToolResult::StructuredJson(Response);
}
