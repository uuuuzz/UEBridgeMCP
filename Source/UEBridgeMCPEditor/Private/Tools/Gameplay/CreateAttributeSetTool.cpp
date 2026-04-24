// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Gameplay/CreateAttributeSetTool.h"

#include "Tools/Gameplay/GASToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "ScopedTransaction.h"

FString UCreateAttributeSetTool::GetToolDescription() const
{
	return TEXT("Create an AttributeSet Blueprint and optionally seed GameplayAttributeData variables.");
}

TMap<FString, FMcpSchemaProperty> UCreateAttributeSetTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("AttributeSet Blueprint asset path"), true));
	Schema.Add(TEXT("parent_class"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional parent class path; defaults to /Script/GameplayAbilities.AttributeSet")));
	Schema.Add(TEXT("attributes"), FMcpSchemaProperty::MakeArray(TEXT("Attribute descriptors with name and optional category"), TEXT("object")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile created Blueprint")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save created Blueprint")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	return Schema;
}

TArray<FString> UCreateAttributeSetTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FMcpToolResult UCreateAttributeSetTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString ParentClassPath = GetStringArgOrDefault(Arguments, TEXT("parent_class"), TEXT("/Script/GameplayAbilities.AttributeSet"));
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	FString Error;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, Error))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), Error);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}
	UClass* ParentClass = GASToolUtils::ResolveSubclass(ParentClassPath, UAttributeSet::StaticClass(), Error);
	if (!ParentClass)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_CLASS_NOT_FOUND"), Error);
	}

	const TArray<TSharedPtr<FJsonValue>>* Attributes = nullptr;
	if (Arguments->TryGetArrayField(TEXT("attributes"), Attributes) && Attributes)
	{
		for (int32 Index = 0; Index < Attributes->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* AttributeObject = nullptr;
			if (!(*Attributes)[Index].IsValid() || !(*Attributes)[Index]->TryGetObject(AttributeObject) || !AttributeObject || !(*AttributeObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("attributes[%d] must be an object"), Index));
			}
			FString AttributeName;
			if (!(*AttributeObject)->TryGetStringField(TEXT("name"), AttributeName) || AttributeName.IsEmpty())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), FString::Printf(TEXT("attributes[%d].name is required"), Index));
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	if (bDryRun)
	{
		return FMcpToolResult::StructuredJson(Response);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Attribute Set")));
	UBlueprint* Blueprint = GASToolUtils::CreateGASBlueprintAsset(AssetPath, ParentClass, Error);
	if (!Blueprint)
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), Error);
	}

	TArray<TSharedPtr<FJsonValue>> CreatedAttributes;
	if (Attributes)
	{
		for (const TSharedPtr<FJsonValue>& AttributeValue : *Attributes)
		{
			const TSharedPtr<FJsonObject>* AttributeObject = nullptr;
			AttributeValue->TryGetObject(AttributeObject);
			const FString AttributeName = GetStringArgOrDefault(*AttributeObject, TEXT("name"));
			const FString Category = GetStringArgOrDefault(*AttributeObject, TEXT("category"), TEXT("Attributes"));
			if (!GASToolUtils::AddAttributeVariable(Blueprint, AttributeName, Category, Error))
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ATTRIBUTE_CREATE_FAILED"), Error);
			}
			TSharedPtr<FJsonObject> CreatedObject = MakeShareable(new FJsonObject);
			CreatedObject->SetStringField(TEXT("name"), AttributeName);
			CreatedObject->SetStringField(TEXT("category"), Category);
			CreatedAttributes.Add(MakeShareable(new FJsonValueObject(CreatedObject)));
		}
	}

	TSharedPtr<FJsonObject> CompileObject;
	if (!GASToolUtils::CompileAndSaveBlueprint(Blueprint, bCompile, bSave, CompileObject, Error))
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_FINALIZE_FAILED"), Error);
	}

	Response->SetArrayField(TEXT("created_attributes"), CreatedAttributes);
	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetObjectField(TEXT("summary"), GASToolUtils::SerializeAttributeSetClass(Blueprint->GeneratedClass, AssetPath));
	return FMcpToolResult::StructuredJson(Response);
}
