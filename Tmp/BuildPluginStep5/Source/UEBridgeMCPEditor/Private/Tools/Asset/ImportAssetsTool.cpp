// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Asset/ImportAssetsTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpPropertySerializer.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/Factory.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"

namespace ImportAssetsToolPrivate
{
	bool EnsureObjectPropertyChain(UObject* RootObject, const FString& PropertyPath, FString& OutError)
	{
		if (!RootObject)
		{
			return true;
		}

		TArray<FString> Segments;
		PropertyPath.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() <= 1)
		{
			return true;
		}

		UObject* CurrentObject = RootObject;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num() - 1; ++SegmentIndex)
		{
			if (!CurrentObject)
			{
				OutError = TEXT("Encountered null object while preparing import option path");
				return false;
			}

			FString SegmentName = Segments[SegmentIndex];
			int32 ArrayMarkerIndex = INDEX_NONE;
			if (SegmentName.FindChar(TEXT('['), ArrayMarkerIndex))
			{
				SegmentName = SegmentName.Left(ArrayMarkerIndex);
			}

			FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(CurrentObject->GetClass(), *SegmentName);
			if (!ObjectProperty)
			{
				return true;
			}

			UObject* NestedObject = ObjectProperty->GetObjectPropertyValue_InContainer(CurrentObject);
			if (!NestedObject)
			{
				UClass* PropertyClass = ObjectProperty->PropertyClass;
				if (!PropertyClass)
				{
					OutError = FString::Printf(TEXT("Object property '%s' has no valid class"), *SegmentName);
					return false;
				}

				NestedObject = NewObject<UObject>(CurrentObject, PropertyClass);
				if (!NestedObject)
				{
					OutError = FString::Printf(TEXT("Failed to instantiate nested import options object '%s'"), *SegmentName);
					return false;
				}

				ObjectProperty->SetObjectPropertyValue_InContainer(CurrentObject, NestedObject);
			}

			CurrentObject = NestedObject;
		}

		return true;
	}

	UObject* ResolvePreferredOptionsObject(UObject* FactoryObject)
	{
		if (!FactoryObject)
		{
			return nullptr;
		}

		static const TCHAR* CandidatePropertyNames[] =
		{
			TEXT("ImportUI"),
			TEXT("Options"),
			TEXT("ReimportUI")
		};

		for (const TCHAR* CandidatePropertyName : CandidatePropertyNames)
		{
			FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(FactoryObject->GetClass(), CandidatePropertyName);
			if (!ObjectProperty)
			{
				continue;
			}

			UObject* CandidateObject = ObjectProperty->GetObjectPropertyValue_InContainer(FactoryObject);
			if (CandidateObject)
			{
				return CandidateObject;
			}
		}

		return nullptr;
	}

	bool ApplyImportOptions(const TArray<UObject*>& TargetObjects, const TSharedPtr<FJsonObject>& ImportOptionsObject, FString& OutError)
	{
		if (!ImportOptionsObject.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ImportOptionsObject->Values)
		{
			bool bApplied = false;
			FString LastResolveError;

			for (UObject* TargetObject : TargetObjects)
			{
				if (!TargetObject)
				{
					continue;
				}

				FString EnsureError;
				if (!EnsureObjectPropertyChain(TargetObject, Pair.Key, EnsureError))
				{
					LastResolveError = EnsureError;
					continue;
				}

				FProperty* Property = nullptr;
				void* Container = nullptr;
				FString PropertyError;
				if (!FMcpAssetModifier::FindPropertyByPath(TargetObject, Pair.Key, Property, Container, PropertyError))
				{
					LastResolveError = PropertyError;
					continue;
				}

				if (!FMcpAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, PropertyError))
				{
					OutError = FString::Printf(TEXT("Failed to set import option '%s': %s"), *Pair.Key, *PropertyError);
					return false;
				}

				bApplied = true;
				break;
			}

			if (!bApplied)
			{
				OutError = FString::Printf(TEXT("Failed to resolve import option '%s': %s"), *Pair.Key, *LastResolveError);
				return false;
			}
		}

		return true;
	}
}

FString UImportAssetsTool::GetToolDescription() const
{
	return TEXT("Import external files (FBX, PNG, JPG, TGA, WAV, etc.) into the project as UE assets. "
		"Supports explicit factory selection and generic import_options mapped through reflection.");
}

TMap<FString, FMcpSchemaProperty> UImportAssetsTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	TSharedPtr<FJsonObject> ImportOptionsRawSchema = MakeShareable(new FJsonObject);
	ImportOptionsRawSchema->SetStringField(TEXT("type"), TEXT("object"));
	ImportOptionsRawSchema->SetBoolField(TEXT("additionalProperties"), true);

	TSharedPtr<FMcpSchemaProperty> ImportOptionsSchema = MakeShared<FMcpSchemaProperty>();
	ImportOptionsSchema->Description = TEXT("Import option overrides. Keys are property paths applied to task options or factory objects, e.g. 'ImportUI.bImportMaterials'.");
	ImportOptionsSchema->RawSchema = ImportOptionsRawSchema;

	TSharedPtr<FMcpSchemaProperty> ImportItemSchema = MakeShared<FMcpSchemaProperty>();
	ImportItemSchema->Type = TEXT("object");
	ImportItemSchema->Description = TEXT("Asset import descriptor");
	ImportItemSchema->NestedRequired = {TEXT("source_file"), TEXT("destination_path")};
	ImportItemSchema->Properties.Add(TEXT("source_file"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Absolute source file path"), true)));
	ImportItemSchema->Properties.Add(TEXT("destination_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Destination package path, e.g. '/Game/Weapons'"), true)));
	ImportItemSchema->Properties.Add(TEXT("destination_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional destination asset name"))));
	ImportItemSchema->Properties.Add(TEXT("replace_existing"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Replace existing assets"))));
	ImportItemSchema->Properties.Add(TEXT("replace_existing_settings"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Replace existing import settings when overwriting"))));
	ImportItemSchema->Properties.Add(TEXT("factory_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional factory class name or class path, e.g. 'FbxFactory' or '/Script/UnrealEd.FbxFactory'"))));
	ImportItemSchema->Properties.Add(TEXT("import_options"), ImportOptionsSchema);

	FMcpSchemaProperty ImportsSchema;
	ImportsSchema.Type = TEXT("array");
	ImportsSchema.Description = TEXT("Array of import descriptors with nested action-specific fields.");
	ImportsSchema.bRequired = true;
	ImportsSchema.Items = ImportItemSchema;
	Schema.Add(TEXT("imports"), ImportsSchema);

	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save imported assets")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Stop batch on first failure and return partial results")));

	return Schema;
}

TArray<FString> UImportAssetsTool::GetRequiredParams() const
{
	return {TEXT("imports")};
}

FMcpToolResult UImportAssetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* ImportsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("imports"), ImportsArray) || !ImportsArray || ImportsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'imports' array is required"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	int32 SucceededCount = 0;
	int32 FailedCount = 0;

	for (int32 ImportIndex = 0; ImportIndex < ImportsArray->Num(); ++ImportIndex)
	{
		const TSharedPtr<FJsonObject>* ImportObject = nullptr;
		if (!(*ImportsArray)[ImportIndex]->TryGetObject(ImportObject) || !(*ImportObject).IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), ImportIndex);

		FString SourceFile;
		FString DestinationPath;
		if (!(*ImportObject)->TryGetStringField(TEXT("source_file"), SourceFile)
			|| !(*ImportObject)->TryGetStringField(TEXT("destination_path"), DestinationPath))
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("'source_file' and 'destination_path' are required"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			FailedCount++;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		ResultObject->SetStringField(TEXT("source_file"), SourceFile);
		ResultObject->SetStringField(TEXT("destination_path"), DestinationPath);

		if (!FPaths::FileExists(SourceFile))
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Source file not found: '%s'"), *SourceFile));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			FailedCount++;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		FString DestinationName;
		(*ImportObject)->TryGetStringField(TEXT("destination_name"), DestinationName);
		ResultObject->SetStringField(TEXT("destination_name"), DestinationName);

		const bool bReplaceExisting = GetBoolArgOrDefault(*ImportObject, TEXT("replace_existing"), false);
		const bool bReplaceExistingSettings = GetBoolArgOrDefault(*ImportObject, TEXT("replace_existing_settings"), false);

		FString FactoryName = GetStringArgOrDefault(*ImportObject, TEXT("factory_name"));
		const TSharedPtr<FJsonObject>* ImportOptionsObject = nullptr;
		(*ImportObject)->TryGetObjectField(TEXT("import_options"), ImportOptionsObject);

		if (bDryRun)
		{
			ResultObject->SetBoolField(TEXT("success"), true);
			ResultObject->SetStringField(TEXT("status"), TEXT("dry_run_validated"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			SucceededCount++;
			continue;
		}

		UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
		ImportTask->Filename = SourceFile;
		ImportTask->DestinationPath = DestinationPath;
		ImportTask->DestinationName = DestinationName;
		ImportTask->bReplaceExisting = bReplaceExisting;
		ImportTask->bReplaceExistingSettings = bReplaceExistingSettings;
		ImportTask->bAutomated = true;
		ImportTask->bSave = bSave;

		if (!FactoryName.IsEmpty())
		{
			FString ResolveError;
			UClass* FactoryClass = FMcpPropertySerializer::ResolveClassOfType<UFactory>(FactoryName, ResolveError);
			if (!FactoryClass)
			{
				ResultObject->SetBoolField(TEXT("success"), false);
				ResultObject->SetStringField(TEXT("error"), ResolveError);
				ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
				bAnyFailed = true;
				FailedCount++;
				if (bRollbackOnError)
				{
					break;
				}
				continue;
			}

			UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
			ImportTask->Factory = Factory;
			ResultObject->SetStringField(TEXT("factory_name"), FactoryClass->GetName());
		}

		if (ImportOptionsObject && (*ImportOptionsObject).IsValid())
		{
			bool bAppliedImportOptions = false;
			FString OptionsError;
			TArray<UObject*> ImportOptionTargets;

			if (ImportTask->Factory)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ImportOptionsObject)->Values)
				{
					if (!ImportAssetsToolPrivate::EnsureObjectPropertyChain(ImportTask->Factory, Pair.Key, OptionsError))
					{
						break;
					}
				}

				if (OptionsError.IsEmpty() && !ImportTask->Options)
				{
					ImportTask->Options = ImportAssetsToolPrivate::ResolvePreferredOptionsObject(ImportTask->Factory);
				}
			}

			if (ImportTask->Factory)
			{
				ImportOptionTargets.Add(ImportTask->Factory);
			}
			if (ImportTask->Options)
			{
				ImportOptionTargets.Add(ImportTask->Options);
			}

			if (OptionsError.IsEmpty() && ImportOptionTargets.Num() > 0)
			{
				bAppliedImportOptions = ImportAssetsToolPrivate::ApplyImportOptions(ImportOptionTargets, *ImportOptionsObject, OptionsError);
			}
			else if (OptionsError.IsEmpty())
			{
				OptionsError = TEXT("'import_options' requires a factory or task options object. Provide 'factory_name' for format-specific settings.");
			}

			if (!bAppliedImportOptions)
			{
				ResultObject->SetBoolField(TEXT("success"), false);
				ResultObject->SetStringField(TEXT("error"), OptionsError);
				ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
				bAnyFailed = true;
				FailedCount++;
				if (bRollbackOnError)
				{
					break;
				}
				continue;
			}
		}

		TArray<UAssetImportTask*> ImportTasks;
		ImportTasks.Add(ImportTask);
		AssetTools.ImportAssetTasks(ImportTasks);

		const TArray<UObject*>& ImportedObjects = ImportTask->GetObjects();
		if (ImportedObjects.Num() == 0)
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("Import failed - no objects were created"));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			bAnyFailed = true;
			FailedCount++;
			if (bRollbackOnError)
			{
				break;
			}
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> ImportedAssetPaths;
		for (UObject* ImportedObject : ImportedObjects)
		{
			if (!ImportedObject)
			{
				continue;
			}

			const FString ImportedPath = ImportedObject->GetPathName();
			ImportedAssetPaths.Add(MakeShareable(new FJsonValueString(ImportedPath)));
			ModifiedAssetsArray.Add(MakeShareable(new FJsonValueString(ImportedPath)));
		}

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetArrayField(TEXT("imported_assets"), ImportedAssetPaths);
		if (ImportedAssetPaths.Num() > 0)
		{
			ResultObject->SetStringField(TEXT("imported_asset"), ImportedAssetPaths[0]->AsString());
		}
		if (ImportTask->ImportedObjectPaths.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ImportedObjectPathValues;
			for (const FString& ImportedObjectPath : ImportTask->ImportedObjectPaths)
			{
				ImportedObjectPathValues.Add(MakeShareable(new FJsonValueString(ImportedObjectPath)));
			}
			ResultObject->SetArrayField(TEXT("imported_object_paths"), ImportedObjectPathValues);
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		SucceededCount++;
	}

	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("total"), ImportsArray->Num());
	SummaryObject->SetNumberField(TEXT("succeeded"), SucceededCount);
	SummaryObject->SetNumberField(TEXT("failed"), FailedCount);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("import-assets"));
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetObjectField(TEXT("summary"), SummaryObject);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}