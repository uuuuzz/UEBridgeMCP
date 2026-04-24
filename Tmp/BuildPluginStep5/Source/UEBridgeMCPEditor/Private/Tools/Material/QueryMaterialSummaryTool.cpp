// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Material/QueryMaterialSummaryTool.h"

#include "Tools/Material/MaterialToolUtils.h"
#include "Session/McpEditorSessionManager.h"
#include "Utils/McpV2ToolUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"

namespace
{
	void AppendScalarParameters(UMaterialInterface* Material, const TSet<FString>& ParameterFilter, bool bIncludeValues, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Guids;
		Material->GetAllScalarParameterInfo(Infos, Guids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			if (ParameterFilter.Num() > 0 && !ParameterFilter.Contains(Info.Name.ToString()))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
			Object->SetStringField(TEXT("name"), Info.Name.ToString());
			Object->SetStringField(TEXT("type"), TEXT("scalar"));
			if (bIncludeValues)
			{
				float Value = 0.0f;
				Material->GetScalarParameterValue(Info, Value);
				Object->SetNumberField(TEXT("value"), Value);
			}
			OutArray.Add(MakeShareable(new FJsonValueObject(Object)));
		}
	}

	void AppendVectorParameters(UMaterialInterface* Material, const TSet<FString>& ParameterFilter, bool bIncludeValues, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Guids;
		Material->GetAllVectorParameterInfo(Infos, Guids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			if (ParameterFilter.Num() > 0 && !ParameterFilter.Contains(Info.Name.ToString()))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
			Object->SetStringField(TEXT("name"), Info.Name.ToString());
			Object->SetStringField(TEXT("type"), TEXT("vector"));
			if (bIncludeValues)
			{
				FLinearColor Value;
				Material->GetVectorParameterValue(Info, Value);
				TSharedPtr<FJsonObject> ValueObject = MakeShareable(new FJsonObject);
				ValueObject->SetNumberField(TEXT("r"), Value.R);
				ValueObject->SetNumberField(TEXT("g"), Value.G);
				ValueObject->SetNumberField(TEXT("b"), Value.B);
				ValueObject->SetNumberField(TEXT("a"), Value.A);
				Object->SetObjectField(TEXT("value"), ValueObject);
			}
			OutArray.Add(MakeShareable(new FJsonValueObject(Object)));
		}
	}

	void AppendTextureParameters(UMaterialInterface* Material, const TSet<FString>& ParameterFilter, bool bIncludeValues, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Guids;
		Material->GetAllTextureParameterInfo(Infos, Guids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			if (ParameterFilter.Num() > 0 && !ParameterFilter.Contains(Info.Name.ToString()))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
			Object->SetStringField(TEXT("name"), Info.Name.ToString());
			Object->SetStringField(TEXT("type"), TEXT("texture"));
			if (bIncludeValues)
			{
				UTexture* Texture = nullptr;
				Material->GetTextureParameterValue(Info, Texture);
				Object->SetStringField(TEXT("value"), Texture ? Texture->GetPathName() : TEXT(""));
			}
			OutArray.Add(MakeShareable(new FJsonValueObject(Object)));
		}
	}
}

FString UQueryMaterialSummaryTool::GetToolDescription() const
{
	return TEXT("Return compact material parameter summaries with optional value expansion, true parameter filtering, and optional graph overview for base materials.");
}

TMap<FString, FMcpSchemaProperty> UQueryMaterialSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Material or material instance asset path"), true));
	Schema.Add(TEXT("parameter_names"), FMcpSchemaProperty::MakeArray(TEXT("Optional explicit parameter names"), TEXT("string")));
	Schema.Add(TEXT("include_values"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include current parameter values")));
	Schema.Add(TEXT("include_graph_overview"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include compact expression graph overview for UMaterial assets")));
	return Schema;
}

TArray<FString> UQueryMaterialSummaryTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FMcpToolResult UQueryMaterialSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeValues = GetBoolArgOrDefault(Arguments, TEXT("include_values"), false);
	const bool bIncludeGraphOverview = GetBoolArgOrDefault(Arguments, TEXT("include_graph_overview"), false);
	const TArray<TSharedPtr<FJsonValue>>* ParameterNamesArray = nullptr;
	TSet<FString> ParameterFilter;
	if (Arguments->TryGetArrayField(TEXT("parameter_names"), ParameterNamesArray) && ParameterNamesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ParameterNamesArray)
		{
			ParameterFilter.Add(Value->AsString());
		}
	}

	FString LoadError;
	UMaterialInterface* Material = FMcpEditorSessionManager::Get().ResolveAsset<UMaterialInterface>(Context.SessionId, AssetPath, LoadError);
	if (!Material)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	TArray<TSharedPtr<FJsonValue>> ParametersArray;
	AppendScalarParameters(Material, ParameterFilter, bIncludeValues, ParametersArray);
	AppendVectorParameters(Material, ParameterFilter, bIncludeValues, ParametersArray);
	AppendTextureParameters(Material, ParameterFilter, bIncludeValues, ParametersArray);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_type"), Material->GetClass()->GetName());
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Material->GetClass()->GetName()));
	Result->SetArrayField(TEXT("parameters"), ParametersArray);
	Result->SetNumberField(TEXT("parameter_count"), ParametersArray.Num());
	if (bIncludeGraphOverview)
	{
		if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
		{
			Result->SetObjectField(TEXT("graph_overview"), MaterialToolUtils::BuildGraphOverview(BaseMaterial, true));
		}
	}
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Material summary ready"));
}
