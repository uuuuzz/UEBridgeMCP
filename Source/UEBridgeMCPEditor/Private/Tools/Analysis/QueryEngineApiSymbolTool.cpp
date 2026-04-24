// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Analysis/QueryEngineApiSymbolTool.h"

#include "Tools/Analysis/EngineApiToolUtils.h"
#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "Subsystems/Subsystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

FString UQueryEngineApiSymbolTool::GetToolDescription() const
{
	return TEXT("Search local Unreal Engine API symbols using loaded reflection data: classes, functions, properties, structs, enums, subsystems, and plugins.");
}

TMap<FString, FMcpSchemaProperty> UQueryEngineApiSymbolTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Search query for symbol name/path/owner/module."), true));
	Schema.Add(TEXT("symbol_types"), FMcpSchemaProperty::MakeArray(TEXT("Optional symbol types: class, function, property, struct, enum, subsystem, plugin. Defaults to all."), TEXT("string")));
	Schema.Add(TEXT("include_metadata"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include selected metadata on function/property results. Default: false.")));
	Schema.Add(TEXT("include_plugins"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include plugin results. Default: true.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum ranked results. Default: 50, max: 500.")));
	Schema.Add(TEXT("max_classes"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum reflected classes to scan for members. Default: 2500, max: 10000.")));
	return Schema;
}

FMcpToolResult UQueryEngineApiSymbolTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	if (Query.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_ARGUMENT"), TEXT("query is required"));
	}
	const TSet<FString> SymbolTypes = EngineApiToolUtils::ReadLowercaseStringSet(Arguments, TEXT("symbol_types"));
	const bool bIncludeMetadata = GetBoolArgOrDefault(Arguments, TEXT("include_metadata"), false);
	const bool bIncludePlugins = GetBoolArgOrDefault(Arguments, TEXT("include_plugins"), true);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 50), 1, 500);
	const int32 MaxClasses = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("max_classes"), 2500), 1, 10000);

	TArray<EngineApiToolUtils::FScoredJson> Items;
	int32 ClassesScanned = 0;
	int32 FunctionsScanned = 0;
	int32 PropertiesScanned = 0;
	int32 StructsScanned = 0;
	int32 EnumsScanned = 0;
	int32 PluginsScanned = 0;
	bool bClassesTruncated = false;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || Class->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			continue;
		}
		if (++ClassesScanned > MaxClasses)
		{
			bClassesTruncated = true;
			break;
		}

		const bool bIsSubsystem = Class->IsChildOf(USubsystem::StaticClass());
		if (EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("class")) || (bIsSubsystem && EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("subsystem"))))
		{
			const FString KindBoost = bIsSubsystem ? TEXT("subsystem") : TEXT("class");
			const double Score = (bIsSubsystem ? 10.0 : 0.0) + SearchToolUtils::ScoreFields({ Class->GetName(), Class->GetPathName(), Class->GetOutermost() ? Class->GetOutermost()->GetName() : FString(), KindBoost }, Query);
			if (Score > 0.0)
			{
				TSharedPtr<FJsonObject> Object = EngineApiToolUtils::SerializeClass(Class, Score);
				Object->SetStringField(TEXT("kind"), bIsSubsystem ? TEXT("subsystem") : TEXT("class"));
				Items.Add({ Score, Object });
			}
		}

		if (EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("function")))
		{
			for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
			{
				UFunction* Function = *FuncIt;
				++FunctionsScanned;
				const double Score = SearchToolUtils::ScoreFields({ Function->GetName(), Function->GetPathName(), Class->GetName(), EngineApiToolUtils::FunctionFlagsToString(Function) }, Query);
				if (Score > 0.0)
				{
					Items.Add({ Score, EngineApiToolUtils::SerializeFunction(Function, Class, bIncludeMetadata, Score) });
				}
			}
		}

		if (EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("property")))
		{
			for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;
				++PropertiesScanned;
				const double Score = SearchToolUtils::ScoreFields({ Property->GetName(), Property->GetPathName(), Class->GetName(), EngineApiToolUtils::PropertyTypeToString(Property) }, Query);
				if (Score > 0.0)
				{
					Items.Add({ Score, EngineApiToolUtils::SerializeProperty(Property, Class, bIncludeMetadata, Score) });
				}
			}
		}
	}

	if (EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("struct")))
	{
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			++StructsScanned;
			const double Score = SearchToolUtils::ScoreFields({ Struct->GetName(), Struct->GetPathName(), Struct->GetOutermost() ? Struct->GetOutermost()->GetName() : FString() }, Query);
			if (Score <= 0.0)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
			Object->SetStringField(TEXT("kind"), TEXT("struct"));
			Object->SetStringField(TEXT("name"), Struct->GetName());
			Object->SetStringField(TEXT("path"), Struct->GetPathName());
			Object->SetStringField(TEXT("module"), Struct->GetOutermost() ? Struct->GetOutermost()->GetName() : FString());
			Object->SetNumberField(TEXT("score"), Score);
			Items.Add({ Score, Object });
		}
	}

	if (EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("enum")))
	{
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			UEnum* Enum = *It;
			++EnumsScanned;
			const double Score = SearchToolUtils::ScoreFields({ Enum->GetName(), Enum->GetPathName(), Enum->GetOutermost() ? Enum->GetOutermost()->GetName() : FString() }, Query);
			if (Score <= 0.0)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
			Object->SetStringField(TEXT("kind"), TEXT("enum"));
			Object->SetStringField(TEXT("name"), Enum->GetName());
			Object->SetStringField(TEXT("path"), Enum->GetPathName());
			Object->SetStringField(TEXT("module"), Enum->GetOutermost() ? Enum->GetOutermost()->GetName() : FString());
			Object->SetNumberField(TEXT("entry_count"), Enum->NumEnums());
			Object->SetNumberField(TEXT("score"), Score);
			Items.Add({ Score, Object });
		}
	}

	if (bIncludePlugins && EngineApiToolUtils::TypeSetAllows(SymbolTypes, TEXT("plugin")))
	{
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
		{
			++PluginsScanned;
			const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
			const double Score = SearchToolUtils::ScoreFields({ Plugin->GetName(), Plugin->GetFriendlyName(), Descriptor.Description, Descriptor.Category }, Query);
			if (Score > 0.0)
			{
				Items.Add({ Score, EngineApiToolUtils::SerializePlugin(Plugin, true, false, Score) });
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	EngineApiToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());
	Result->SetBoolField(TEXT("classes_truncated"), bClassesTruncated);
	Result->SetNumberField(TEXT("classes_scanned"), ClassesScanned);
	Result->SetNumberField(TEXT("functions_scanned"), FunctionsScanned);
	Result->SetNumberField(TEXT("properties_scanned"), PropertiesScanned);
	Result->SetNumberField(TEXT("structs_scanned"), StructsScanned);
	Result->SetNumberField(TEXT("enums_scanned"), EnumsScanned);
	Result->SetNumberField(TEXT("plugins_scanned"), PluginsScanned);
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Engine API symbol search complete"));
}
