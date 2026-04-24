// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Analysis/QueryEditorSubsystemSummaryTool.h"

#include "Tools/Analysis/EngineApiToolUtils.h"
#include "Tools/McpToolResult.h"
#include "Tools/Search/SearchToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "EditorSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/Subsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FString SubsystemFamily(UClass* Class)
	{
		if (!Class)
		{
			return TEXT("unknown");
		}
		if (Class->IsChildOf(UEditorSubsystem::StaticClass()))
		{
			return TEXT("editor");
		}
		if (Class->IsChildOf(UEngineSubsystem::StaticClass()))
		{
			return TEXT("engine");
		}
		if (Class->IsChildOf(UWorldSubsystem::StaticClass()))
		{
			return TEXT("world");
		}
		if (Class->IsChildOf(UGameInstanceSubsystem::StaticClass()))
		{
			return TEXT("game_instance");
		}
		return TEXT("other");
	}

	bool FamilyAllowed(const TSet<FString>& Families, const FString& Family)
	{
		return Families.Num() == 0 || Families.Contains(Family);
	}

	UObject* ResolveSubsystemInstance(UClass* Class, UWorld* World)
	{
		if (!Class)
		{
			return nullptr;
		}
		if (Class->IsChildOf(UEditorSubsystem::StaticClass()) && GEditor)
		{
			return GEditor->GetEditorSubsystemBase(Class);
		}
		if (Class->IsChildOf(UEngineSubsystem::StaticClass()) && GEngine)
		{
			return GEngine->GetEngineSubsystemBase(Class);
		}
		if (Class->IsChildOf(UWorldSubsystem::StaticClass()) && World)
		{
			return World->GetSubsystemBase(Class);
		}
		if (Class->IsChildOf(UGameInstanceSubsystem::StaticClass()) && World && World->GetGameInstance())
		{
			return World->GetGameInstance()->GetSubsystemBase(Class);
		}
		return nullptr;
	}
}

FString UQueryEditorSubsystemSummaryTool::GetToolDescription() const
{
	return TEXT("List local Editor/Engine/World/GameInstance subsystem classes with instance availability and lightweight class metadata.");
}

TMap<FString, FMcpSchemaProperty> UQueryEditorSubsystemSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("query"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional ranked query across subsystem class name/path/family.")));
	Schema.Add(TEXT("families"), FMcpSchemaProperty::MakeArray(TEXT("Optional families: editor, engine, world, game_instance, other. Defaults to all."), TEXT("string")));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World used for world/game_instance subsystem instance checks. Default: editor."), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("include_inactive"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include subsystem classes without a current instance. Default: true.")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum subsystem results. Default: 100, max: 500.")));
	return Schema;
}

FMcpToolResult UQueryEditorSubsystemSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Query = GetStringArgOrDefault(Arguments, TEXT("query"));
	const TSet<FString> Families = EngineApiToolUtils::ReadLowercaseStringSet(Arguments, TEXT("families"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bIncludeInactive = GetBoolArgOrDefault(Arguments, TEXT("include_inactive"), true);
	const int32 Limit = FMath::Clamp(GetIntArgOrDefault(Arguments, TEXT("limit"), 100), 1, 500);
	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType);

	TArray<EngineApiToolUtils::FScoredJson> Items;
	int32 Scanned = 0;
	int32 ActiveCount = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class || !Class->IsChildOf(USubsystem::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists))
		{
			continue;
		}
		++Scanned;
		const FString Family = SubsystemFamily(Class);
		if (!FamilyAllowed(Families, Family))
		{
			continue;
		}
		UObject* Instance = ResolveSubsystemInstance(Class, World);
		if (Instance)
		{
			++ActiveCount;
		}
		if (!bIncludeInactive && !Instance)
		{
			continue;
		}

		const double Score = SearchToolUtils::ScoreFields({ Class->GetName(), Class->GetPathName(), Family }, Query);
		if (!Query.IsEmpty() && Score <= 0.0)
		{
			continue;
		}

		TSharedPtr<FJsonObject> Object = EngineApiToolUtils::SerializeClass(Class, Score);
		Object->SetStringField(TEXT("kind"), TEXT("subsystem"));
		Object->SetStringField(TEXT("family"), Family);
		Object->SetBoolField(TEXT("active"), Instance != nullptr);
		if (Instance)
		{
			Object->SetStringField(TEXT("instance_path"), Instance->GetPathName());
		}
		Items.Add({ Score, Object });
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	EngineApiToolUtils::SortAndTrim(Items, Limit, Results);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetStringField(TEXT("world"), World ? World->GetPathName() : FString());
	Result->SetArrayField(TEXT("subsystems"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	Result->SetNumberField(TEXT("total_matches"), Items.Num());
	Result->SetNumberField(TEXT("subsystems_scanned"), Scanned);
	Result->SetNumberField(TEXT("active_count"), ActiveCount);
	Result->SetBoolField(TEXT("truncated"), Items.Num() > Results.Num());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Editor subsystem summary ready"));
}
