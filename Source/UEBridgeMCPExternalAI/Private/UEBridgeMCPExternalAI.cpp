// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "UEBridgeMCPExternalAI.h"

#include "Tools/McpToolRegistry.h"
#include "Tools/ExternalAI/GenerateExternalAssetTool.h"
#include "Tools/ExternalAI/GenerateExternalContentTool.h"
#include "Dom/JsonObject.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY(LogUEBridgeMCPExternalAI);

namespace
{
	TSharedPtr<FJsonObject> CloneArguments(const TSharedPtr<FJsonObject>& Arguments)
	{
		TSharedPtr<FJsonObject> ClonedArguments = MakeShareable(new FJsonObject);
		if (Arguments.IsValid())
		{
			ClonedArguments->Values = Arguments->Values;
		}
		return ClonedArguments;
	}

	FString FirstStringField(const TSharedPtr<FJsonObject>& Arguments, std::initializer_list<const TCHAR*> FieldNames)
	{
		if (!Arguments.IsValid())
		{
			return FString();
		}

		for (const TCHAR* FieldName : FieldNames)
		{
			FString Value;
			if (Arguments->TryGetStringField(FieldName, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	TSharedPtr<FJsonObject> AdaptExternalAssetArguments(
		const TSharedPtr<FJsonObject>& Arguments,
		const FString& AssetType,
		const FString& DefaultBrief)
	{
		TSharedPtr<FJsonObject> AdaptedArguments = CloneArguments(Arguments);
		AdaptedArguments->SetStringField(TEXT("asset_type"), AssetType);

		if (!AdaptedArguments->HasField(TEXT("brief")))
		{
			FString Brief = FirstStringField(Arguments, { TEXT("prompt"), TEXT("description"), TEXT("text"), TEXT("brief") });
			if (Brief.IsEmpty())
			{
				Brief = DefaultBrief;
			}

			const FString SourceImage = FirstStringField(Arguments, { TEXT("image_path"), TEXT("reference_image"), TEXT("input_image"), TEXT("path") });
			if (!SourceImage.IsEmpty())
			{
				Brief += FString::Printf(TEXT("\nSource image: %s"), *SourceImage);
			}
			AdaptedArguments->SetStringField(TEXT("brief"), Brief);
		}

		if (!AdaptedArguments->HasField(TEXT("reference_prompt")))
		{
			const FString ReferencePrompt = FirstStringField(Arguments, { TEXT("reference_prompt"), TEXT("style"), TEXT("negative_prompt") });
			const FString SourceImage = FirstStringField(Arguments, { TEXT("image_path"), TEXT("reference_image"), TEXT("input_image"), TEXT("path") });
			if (!ReferencePrompt.IsEmpty() || !SourceImage.IsEmpty())
			{
				FString CombinedReference = ReferencePrompt;
				if (!SourceImage.IsEmpty())
				{
					if (!CombinedReference.IsEmpty())
					{
						CombinedReference += TEXT("\n");
					}
					CombinedReference += FString::Printf(TEXT("Reference image path: %s"), *SourceImage);
				}
				AdaptedArguments->SetStringField(TEXT("reference_prompt"), CombinedReference);
			}
		}

		if (!AdaptedArguments->HasField(TEXT("import_destination")))
		{
			const FString ImportDestination = FirstStringField(Arguments, { TEXT("import_destination"), TEXT("destination"), TEXT("destination_path"), TEXT("content_path") });
			if (!ImportDestination.IsEmpty())
			{
				AdaptedArguments->SetStringField(TEXT("import_destination"), ImportDestination);
			}
		}

		return AdaptedArguments;
	}

	void RegisterExternalAssetAlias(
		FMcpToolRegistry& Registry,
		const FString& AliasName,
		const FString& AssetType,
		const FString& DefaultBrief)
	{
		Registry.RegisterToolAlias(AliasName, TEXT("generate-external-asset"));
		Registry.RegisterToolAliasArgumentAdapter(
			AliasName,
			[AssetType, DefaultBrief](const TSharedPtr<FJsonObject>& Arguments)
			{
				return AdaptExternalAssetArguments(Arguments, AssetType, DefaultBrief);
			});
	}
}

void FUEBridgeMCPExternalAIModule::StartupModule()
{
	UE_LOG(LogUEBridgeMCPExternalAI, Log, TEXT("UEBridgeMCPExternalAI module starting up"));

	FMcpToolRegistry& Registry = FMcpToolRegistry::Get();
	Registry.RegisterToolClass(UGenerateExternalContentTool::StaticClass());
	Registry.RegisterToolClass(UGenerateExternalAssetTool::StaticClass());
	RegisterExternalAssetAlias(Registry, TEXT("generate_ui_image"), TEXT("ui_image"), TEXT("Generate a UI image asset for Unreal Engine."));
	RegisterExternalAssetAlias(Registry, TEXT("remove_background"), TEXT("image"), TEXT("Remove the background from the provided image."));
	RegisterExternalAssetAlias(Registry, TEXT("generate_3d_model"), TEXT("static_mesh"), TEXT("Generate a 3D static mesh asset for Unreal Engine."));
	RegisterExternalAssetAlias(Registry, TEXT("image_to_3d_model"), TEXT("static_mesh"), TEXT("Generate a 3D static mesh asset from the provided image."));
	Registry.FindTool(TEXT("generate-external-content"));
	Registry.FindTool(TEXT("generate-external-asset"));
}

void FUEBridgeMCPExternalAIModule::ShutdownModule()
{
	UE_LOG(LogUEBridgeMCPExternalAI, Log, TEXT("UEBridgeMCPExternalAI module shutting down"));

	if (FModuleManager::Get().IsModuleLoaded(TEXT("UEBridgeMCP")))
	{
		FMcpToolRegistry::Get().UnregisterTool(TEXT("generate-external-content"));
		FMcpToolRegistry::Get().UnregisterTool(TEXT("generate-external-asset"));
	}
}

IMPLEMENT_MODULE(FUEBridgeMCPExternalAIModule, UEBridgeMCPExternalAI)
