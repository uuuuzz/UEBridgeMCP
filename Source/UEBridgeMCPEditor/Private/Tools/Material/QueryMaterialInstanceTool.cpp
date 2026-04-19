// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/QueryMaterialInstanceTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Utils/McpV2ToolUtils.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"

namespace
{
	TSharedPtr<FJsonObject> BuildOverride(const FString& Name, const FString& Type, const FString& Value)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("name"), Name);
		Object->SetStringField(TEXT("type"), Type);
		Object->SetStringField(TEXT("value"), Value);
		return Object;
	}
}

FString UQueryMaterialInstanceTool::GetToolDescription() const
{
	return TEXT("Return material instance parent info and explicit parameter overrides.");
}

TMap<FString, FMcpSchemaProperty> UQueryMaterialInstanceTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Material instance asset path"), true));
	return Schema;
}

TArray<FString> UQueryMaterialInstanceTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult UQueryMaterialInstanceTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	FString LoadError;
	// P2-N5: 先用父类 UMaterialInstance 解析，失败再细分错误；
	// 若用户传入的是 UMaterial 等非 MaterialInstance 资产，可以给出更清晰的错误消息。
	UObject* Loaded = FMcpEditorSessionManager::Get().ResolveAsset(Context.SessionId, AssetPath, LoadError);
	if (!Loaded)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Loaded);
	if (!MaterialInstance)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		Details->SetStringField(TEXT("actual_class"), Loaded->GetClass()->GetName());
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_TYPE_MISMATCH"),
			FString::Printf(TEXT("Asset '%s' is of class '%s', expected UMaterialInstance (constant or dynamic)."),
				*AssetPath, *Loaded->GetClass()->GetName()),
			Details);
	}

	TArray<TSharedPtr<FJsonValue>> OverridesArray;
	for (const FScalarParameterValue& Value : MaterialInstance->ScalarParameterValues)
	{
		OverridesArray.Add(MakeShareable(new FJsonValueObject(BuildOverride(Value.ParameterInfo.Name.ToString(), TEXT("scalar"), FString::SanitizeFloat(Value.ParameterValue)))));
	}
	for (const FVectorParameterValue& Value : MaterialInstance->VectorParameterValues)
	{
		OverridesArray.Add(MakeShareable(new FJsonValueObject(BuildOverride(Value.ParameterInfo.Name.ToString(), TEXT("vector"), Value.ParameterValue.ToString()))));
	}
	for (const FTextureParameterValue& Value : MaterialInstance->TextureParameterValues)
	{
		OverridesArray.Add(MakeShareable(new FJsonValueObject(BuildOverride(Value.ParameterInfo.Name.ToString(), TEXT("texture"), Value.ParameterValue ? Value.ParameterValue->GetPathName() : TEXT("")))));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, MaterialInstance->GetClass()->GetName()));
	Result->SetStringField(TEXT("parent_material"), MaterialInstance->Parent ? MaterialInstance->Parent->GetPathName() : TEXT(""));
	Result->SetArrayField(TEXT("overrides"), OverridesArray);
	Result->SetNumberField(TEXT("override_count"), OverridesArray.Num());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Material instance detail ready"));
}
