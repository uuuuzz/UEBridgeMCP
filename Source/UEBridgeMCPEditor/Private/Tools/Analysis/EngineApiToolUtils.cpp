// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Analysis/EngineApiToolUtils.h"

#include "Tools/Search/SearchToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "ModuleDescriptor.h"
#include "Modules/ModuleManager.h"
#include "PluginDescriptor.h"
#include "UObject/EnumProperty.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	FString BoolToString(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	void AddFlag(TArray<FString>& Flags, bool bCondition, const TCHAR* Name)
	{
		if (bCondition)
		{
			Flags.Add(Name);
		}
	}

	FString JoinFlags(const TArray<FString>& Flags)
	{
		return FString::Join(Flags, TEXT("|"));
	}

	FString PluginTypeToString(EPluginType Type)
	{
		switch (Type)
		{
		case EPluginType::Engine: return TEXT("Engine");
		case EPluginType::Enterprise: return TEXT("Enterprise");
		case EPluginType::Project: return TEXT("Project");
		case EPluginType::External: return TEXT("External");
		case EPluginType::Mod: return TEXT("Mod");
		default: return TEXT("Unknown");
		}
	}

	FString PluginLoadedFromToString(EPluginLoadedFrom LoadedFrom)
	{
		switch (LoadedFrom)
		{
		case EPluginLoadedFrom::Engine: return TEXT("Engine");
		case EPluginLoadedFrom::Project: return TEXT("Project");
		default: return TEXT("Unknown");
		}
	}

	FString ModuleHostTypeToString(EHostType::Type Type)
	{
		switch (Type)
		{
		case EHostType::Runtime: return TEXT("Runtime");
		case EHostType::RuntimeNoCommandlet: return TEXT("RuntimeNoCommandlet");
		case EHostType::RuntimeAndProgram: return TEXT("RuntimeAndProgram");
		case EHostType::CookedOnly: return TEXT("CookedOnly");
		case EHostType::UncookedOnly: return TEXT("UncookedOnly");
		case EHostType::Developer: return TEXT("Developer");
		case EHostType::DeveloperTool: return TEXT("DeveloperTool");
		case EHostType::Editor: return TEXT("Editor");
		case EHostType::EditorNoCommandlet: return TEXT("EditorNoCommandlet");
		case EHostType::EditorAndProgram: return TEXT("EditorAndProgram");
		case EHostType::Program: return TEXT("Program");
		case EHostType::ServerOnly: return TEXT("ServerOnly");
		case EHostType::ClientOnly: return TEXT("ClientOnly");
		case EHostType::ClientOnlyNoCommandlet: return TEXT("ClientOnlyNoCommandlet");
		case EHostType::Max: return TEXT("Max");
		default: return TEXT("Unknown");
		}
	}

	FString LoadingPhaseToString(ELoadingPhase::Type Phase)
	{
		switch (Phase)
		{
		case ELoadingPhase::EarliestPossible: return TEXT("EarliestPossible");
		case ELoadingPhase::PostConfigInit: return TEXT("PostConfigInit");
		case ELoadingPhase::PostSplashScreen: return TEXT("PostSplashScreen");
		case ELoadingPhase::PreEarlyLoadingScreen: return TEXT("PreEarlyLoadingScreen");
		case ELoadingPhase::PreLoadingScreen: return TEXT("PreLoadingScreen");
		case ELoadingPhase::PreDefault: return TEXT("PreDefault");
		case ELoadingPhase::Default: return TEXT("Default");
		case ELoadingPhase::PostDefault: return TEXT("PostDefault");
		case ELoadingPhase::PostEngineInit: return TEXT("PostEngineInit");
		case ELoadingPhase::None: return TEXT("None");
		case ELoadingPhase::Max: return TEXT("Max");
		default: return TEXT("Unknown");
		}
	}

	template<typename TFieldLike>
	TSharedPtr<FJsonObject> MetadataToJson(const TFieldLike* Field, const TArray<FName>& Keys)
	{
		TSharedPtr<FJsonObject> Metadata = MakeShareable(new FJsonObject);
		if (!Field)
		{
			return Metadata;
		}
		for (const FName& Key : Keys)
		{
			const FString Value = Field->GetMetaData(Key);
			if (!Value.IsEmpty())
			{
				Metadata->SetStringField(Key.ToString(), Value);
			}
		}
		return Metadata;
	}

	UClass* TryLoadBlueprintGeneratedClass(const FString& ClassName)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.AssetName.ToString().Equals(ClassName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
			if (Blueprint && Blueprint->GeneratedClass)
			{
				return Blueprint->GeneratedClass;
			}
		}
		return nullptr;
	}
}

namespace EngineApiToolUtils
{
	UClass* ResolveClass(const FString& ClassName, FString& OutError)
	{
		if (ClassName.IsEmpty())
		{
			OutError = TEXT("class_name is required");
			return nullptr;
		}

		if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassName))
		{
			return LoadedClass;
		}

		if (UClass* Existing = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None))
		{
			return Existing;
		}

		if (!ClassName.StartsWith(TEXT("U")) && !ClassName.StartsWith(TEXT("A")))
		{
			if (UClass* Existing = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::None))
			{
				return Existing;
			}
			if (UClass* Existing = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::None))
			{
				return Existing;
			}
		}

		if (UClass* BlueprintClass = TryLoadBlueprintGeneratedClass(ClassName))
		{
			return BlueprintClass;
		}

		OutError = FString::Printf(TEXT("Class '%s' not found"), *ClassName);
		return nullptr;
	}

	FString ClassKind(UClass* Class)
	{
		if (!Class)
		{
			return TEXT("unknown");
		}
		if (Class->IsChildOf(UBlueprintGeneratedClass::StaticClass()))
		{
			return TEXT("blueprint");
		}
		return Class->HasAnyClassFlags(CLASS_Native) ? TEXT("native") : TEXT("script");
	}

	FString ClassFlagsToString(UClass* Class)
	{
		TArray<FString> Flags;
		AddFlag(Flags, Class && Class->HasAnyClassFlags(CLASS_Abstract), TEXT("abstract"));
		AddFlag(Flags, Class && Class->HasAnyClassFlags(CLASS_Deprecated), TEXT("deprecated"));
		AddFlag(Flags, Class && Class->HasAnyClassFlags(CLASS_Native), TEXT("native"));
		AddFlag(Flags, Class && Class->HasAnyClassFlags(CLASS_Config), TEXT("config"));
		AddFlag(Flags, Class && Class->GetBoolMetaData(TEXT("BlueprintType")), TEXT("blueprint_type"));
		AddFlag(Flags, Class && Class->GetBoolMetaData(TEXT("IsBlueprintBase")), TEXT("blueprintable"));
		return JoinFlags(Flags);
	}

	FString FunctionFlagsToString(UFunction* Function)
	{
		TArray<FString> Flags;
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Public), TEXT("public"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Protected), TEXT("protected"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Private), TEXT("private"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Static), TEXT("static"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Native), TEXT("native"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable), TEXT("blueprint_callable"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure), TEXT("blueprint_pure"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_BlueprintEvent), TEXT("blueprint_event"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Exec), TEXT("exec"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Const), TEXT("const"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_Net), TEXT("net"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_NetServer), TEXT("server"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_NetClient), TEXT("client"));
		AddFlag(Flags, Function && Function->HasAnyFunctionFlags(FUNC_NetMulticast), TEXT("multicast"));
		return JoinFlags(Flags);
	}

	FString PropertyFlagsToString(FProperty* Property)
	{
		TArray<FString> Flags;
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_Edit), TEXT("edit"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_BlueprintVisible), TEXT("blueprint_visible"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly), TEXT("blueprint_read_only"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_Config), TEXT("config"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_Transient), TEXT("transient"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance), TEXT("disable_edit_on_instance"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_DisableEditOnTemplate), TEXT("disable_edit_on_template"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_RepSkip), TEXT("rep_skip"));
		AddFlag(Flags, Property && Property->HasAnyPropertyFlags(CPF_Net), TEXT("net"));
		return JoinFlags(Flags);
	}

	FString PropertyTypeToString(FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("unknown");
		}
		FString ExtendedType;
		return Property->GetCPPType(&ExtendedType) + ExtendedType;
	}

	TSharedPtr<FJsonObject> SerializeClass(UClass* Class, double Score)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Class)
		{
			return Object;
		}
		Object->SetStringField(TEXT("kind"), TEXT("class"));
		Object->SetStringField(TEXT("name"), Class->GetName());
		Object->SetStringField(TEXT("path"), Class->GetPathName());
		Object->SetStringField(TEXT("module"), Class->GetOutermost() ? Class->GetOutermost()->GetName() : FString());
		Object->SetStringField(TEXT("class_kind"), ClassKind(Class));
		Object->SetStringField(TEXT("flags"), ClassFlagsToString(Class));
		Object->SetBoolField(TEXT("is_abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
		Object->SetBoolField(TEXT("is_deprecated"), Class->HasAnyClassFlags(CLASS_Deprecated));
		Object->SetBoolField(TEXT("is_blueprint_type"), Class->GetBoolMetaData(TEXT("BlueprintType")));
		Object->SetBoolField(TEXT("is_blueprintable"), Class->GetBoolMetaData(TEXT("IsBlueprintBase")));
		Object->SetNumberField(TEXT("score"), Score);
		if (UClass* SuperClass = Class->GetSuperClass())
		{
			Object->SetStringField(TEXT("super_class"), SuperClass->GetName());
			Object->SetStringField(TEXT("super_class_path"), SuperClass->GetPathName());
		}
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeFunction(UFunction* Function, UClass* OwnerClass, bool bIncludeMetadata, double Score)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Function)
		{
			return Object;
		}
		Object->SetStringField(TEXT("kind"), TEXT("function"));
		Object->SetStringField(TEXT("name"), Function->GetName());
		Object->SetStringField(TEXT("path"), Function->GetPathName());
		Object->SetStringField(TEXT("owner_class"), OwnerClass ? OwnerClass->GetName() : FString());
		Object->SetStringField(TEXT("owner_class_path"), OwnerClass ? OwnerClass->GetPathName() : FString());
		Object->SetStringField(TEXT("flags"), FunctionFlagsToString(Function));
		Object->SetNumberField(TEXT("score"), Score);
		Object->SetBoolField(TEXT("blueprint_callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
		Object->SetBoolField(TEXT("blueprint_pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
		Object->SetBoolField(TEXT("native"), Function->HasAnyFunctionFlags(FUNC_Native));

		TArray<TSharedPtr<FJsonValue>> Params;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Param = *It;
			if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}
			TSharedPtr<FJsonObject> ParamObject = MakeShareable(new FJsonObject);
			ParamObject->SetStringField(TEXT("name"), Param->GetName());
			ParamObject->SetStringField(TEXT("type"), PropertyTypeToString(Param));
			ParamObject->SetBoolField(TEXT("is_return"), Param->HasAnyPropertyFlags(CPF_ReturnParm));
			ParamObject->SetBoolField(TEXT("is_out"), Param->HasAnyPropertyFlags(CPF_OutParm));
			ParamObject->SetBoolField(TEXT("is_const"), Param->HasAnyPropertyFlags(CPF_ConstParm));
			Params.Add(MakeShareable(new FJsonValueObject(ParamObject)));
		}
		Object->SetArrayField(TEXT("parameters"), Params);
		Object->SetNumberField(TEXT("parameter_count"), Params.Num());

		if (bIncludeMetadata)
		{
			Object->SetObjectField(TEXT("metadata"), MetadataToJson(Function, { TEXT("DisplayName"), TEXT("Category"), TEXT("ToolTip"), TEXT("Keywords"), TEXT("CompactNodeTitle") }));
		}
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeProperty(FProperty* Property, UClass* OwnerClass, bool bIncludeMetadata, double Score)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Property)
		{
			return Object;
		}
		Object->SetStringField(TEXT("kind"), TEXT("property"));
		Object->SetStringField(TEXT("name"), Property->GetName());
		Object->SetStringField(TEXT("path"), Property->GetPathName());
		Object->SetStringField(TEXT("owner_class"), OwnerClass ? OwnerClass->GetName() : FString());
		Object->SetStringField(TEXT("owner_class_path"), OwnerClass ? OwnerClass->GetPathName() : FString());
		Object->SetStringField(TEXT("type"), PropertyTypeToString(Property));
		Object->SetStringField(TEXT("flags"), PropertyFlagsToString(Property));
		Object->SetNumberField(TEXT("score"), Score);
		Object->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
		Object->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		Object->SetBoolField(TEXT("blueprint_read_only"), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));

		if (bIncludeMetadata)
		{
			Object->SetObjectField(TEXT("metadata"), MetadataToJson(Property, { TEXT("DisplayName"), TEXT("Category"), TEXT("ToolTip"), TEXT("ClampMin"), TEXT("ClampMax"), TEXT("Units") }));
		}
		return Object;
	}

	TSharedPtr<FJsonObject> SerializePlugin(const TSharedRef<IPlugin>& Plugin, bool bIncludeModules, bool bIncludePaths, double Score)
	{
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetStringField(TEXT("kind"), TEXT("plugin"));
		Object->SetStringField(TEXT("name"), Plugin->GetName());
		Object->SetStringField(TEXT("friendly_name"), Plugin->GetFriendlyName());
		Object->SetStringField(TEXT("description"), Descriptor.Description);
		Object->SetStringField(TEXT("category"), Descriptor.Category);
		Object->SetStringField(TEXT("version_name"), Descriptor.VersionName);
		Object->SetNumberField(TEXT("version"), Descriptor.Version);
		Object->SetStringField(TEXT("created_by"), Descriptor.CreatedBy);
		Object->SetStringField(TEXT("type"), PluginTypeToString(Plugin->GetType()));
		Object->SetStringField(TEXT("loaded_from"), PluginLoadedFromToString(Plugin->GetLoadedFrom()));
		Object->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
		Object->SetBoolField(TEXT("mounted"), Plugin->IsMounted());
		Object->SetBoolField(TEXT("hidden"), Plugin->IsHidden());
		Object->SetBoolField(TEXT("can_contain_content"), Plugin->CanContainContent());
		Object->SetBoolField(TEXT("can_contain_verse"), Plugin->CanContainVerse());
		Object->SetBoolField(TEXT("installed"), Descriptor.bInstalled);
		Object->SetNumberField(TEXT("score"), Score);
		if (!Plugin->GetDeprecatedEngineVersion().IsEmpty())
		{
			Object->SetStringField(TEXT("deprecated_engine_version"), Plugin->GetDeprecatedEngineVersion());
		}
		if (bIncludePaths)
		{
			Object->SetStringField(TEXT("descriptor_file"), Plugin->GetDescriptorFileName());
			Object->SetStringField(TEXT("base_dir"), Plugin->GetBaseDir());
			Object->SetStringField(TEXT("content_dir"), Plugin->GetContentDir());
			Object->SetStringField(TEXT("mounted_asset_path"), Plugin->GetMountedAssetPath());
		}
		if (bIncludeModules)
		{
			TArray<TSharedPtr<FJsonValue>> Modules;
			for (const FModuleDescriptor& Module : Descriptor.Modules)
			{
				TSharedPtr<FJsonObject> ModuleObject = MakeShareable(new FJsonObject);
				ModuleObject->SetStringField(TEXT("name"), Module.Name.ToString());
				ModuleObject->SetStringField(TEXT("type"), ModuleHostTypeToString(Module.Type));
				ModuleObject->SetStringField(TEXT("loading_phase"), LoadingPhaseToString(Module.LoadingPhase));
				ModuleObject->SetBoolField(TEXT("loaded"), FModuleManager::Get().IsModuleLoaded(Module.Name));
				Modules.Add(MakeShareable(new FJsonValueObject(ModuleObject)));
			}
			Object->SetArrayField(TEXT("modules"), Modules);
			Object->SetNumberField(TEXT("module_count"), Modules.Num());
		}
		return Object;
	}

	void SortAndTrim(TArray<FScoredJson>& Items, int32 Limit, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		Items.Sort([](const FScoredJson& A, const FScoredJson& B)
		{
			return A.Score > B.Score;
		});
		const int32 MaxItems = FMath::Max(1, Limit);
		for (int32 Index = 0; Index < Items.Num() && Index < MaxItems; ++Index)
		{
			if (Items[Index].Object.IsValid())
			{
				OutArray.Add(MakeShareable(new FJsonValueObject(Items[Index].Object)));
			}
		}
	}

	void ExtractStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName, TArray<FString>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, ArrayField) || !ArrayField)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *ArrayField)
		{
			if (Value.IsValid())
			{
				FString StringValue = Value->AsString();
				if (!StringValue.IsEmpty())
				{
					OutValues.Add(StringValue);
				}
			}
		}
	}

	bool TypeSetAllows(const TSet<FString>& Types, const FString& Type)
	{
		return Types.Num() == 0 || Types.Contains(Type);
	}

	TSet<FString> ReadLowercaseStringSet(const TSharedPtr<FJsonObject>& Arguments, const FString& FieldName)
	{
		TArray<FString> Values;
		ExtractStringArrayField(Arguments, FieldName, Values);
		TSet<FString> Result;
		for (FString Value : Values)
		{
			Value.ToLowerInline();
			Result.Add(Value);
		}
		return Result;
	}
}
