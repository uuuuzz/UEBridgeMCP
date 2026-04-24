// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryLevelTool.h"
#include "Tools/PIE/PieSessionTool.h"
#include "Utils/McpAssetModifier.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Tools/McpToolResult.h"
#include "UEBridgeMCPEditor.h"
#include "EngineUtils.h" // For TActorIterator

FString UQueryLevelTool::GetToolDescription() const
{
	return TEXT("Query actors in levels. Use 'level_path' to query any level asset without opening it. "
		"Use 'world' param: 'editor' (default), 'pie', or 'auto'. "
		"Returns actor names, classes, transforms, and optionally components. "
		"When actor_name is specified, returns detailed info for that specific actor including properties.");
}

TMap<FString, FMcpSchemaProperty> UQueryLevelTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	// External level parameter
	FMcpSchemaProperty LevelPath;
	LevelPath.Type = TEXT("string");
	LevelPath.Description = TEXT("Asset path to query (e.g., '/Game/Maps/Level2'). If omitted, queries the current editor/PIE world.");
	LevelPath.bRequired = false;
	Schema.Add(TEXT("level_path"), LevelPath);

	// Detail mode parameter
	FMcpSchemaProperty ActorName;
	ActorName.Type = TEXT("string");
	ActorName.Description = TEXT("Get detailed info for a specific actor by name or label (wildcards supported, e.g., '*Demon*'). Returns first match.");
	ActorName.bRequired = false;
	Schema.Add(TEXT("actor_name"), ActorName);

	// List mode parameters
	FMcpSchemaProperty ClassFilter;
	ClassFilter.Type = TEXT("string");
	ClassFilter.Description = TEXT("Filter by actor class (wildcards supported, e.g., '*Light*', 'StaticMeshActor')");
	ClassFilter.bRequired = false;
	Schema.Add(TEXT("class_filter"), ClassFilter);

	FMcpSchemaProperty FolderFilter;
	FolderFilter.Type = TEXT("string");
	FolderFilter.Description = TEXT("Filter by World Outliner folder path");
	FolderFilter.bRequired = false;
	Schema.Add(TEXT("folder_filter"), FolderFilter);

	FMcpSchemaProperty TagFilter;
	TagFilter.Type = TEXT("string");
	TagFilter.Description = TEXT("Filter by actor tag");
	TagFilter.bRequired = false;
	Schema.Add(TEXT("tag_filter"), TagFilter);

	FMcpSchemaProperty IncludeHidden;
	IncludeHidden.Type = TEXT("boolean");
	IncludeHidden.Description = TEXT("Include hidden actors in results (default: false)");
	IncludeHidden.bRequired = false;
	Schema.Add(TEXT("include_hidden"), IncludeHidden);

	// Shared parameters
	FMcpSchemaProperty IncludeComponents;
	IncludeComponents.Type = TEXT("boolean");
	IncludeComponents.Description = TEXT("Include component list for each actor (default: false)");
	IncludeComponents.bRequired = false;
	Schema.Add(TEXT("include_components"), IncludeComponents);

	FMcpSchemaProperty IncludeTransform;
	IncludeTransform.Type = TEXT("boolean");
	IncludeTransform.Description = TEXT("Include actor transforms (default: true)");
	IncludeTransform.bRequired = false;
	Schema.Add(TEXT("include_transform"), IncludeTransform);

	// Detail mode parameters
	FMcpSchemaProperty IncludeProperties;
	IncludeProperties.Type = TEXT("boolean");
	IncludeProperties.Description = TEXT("Include actor properties via reflection (default: false)");
	IncludeProperties.bRequired = false;
	Schema.Add(TEXT("include_properties"), IncludeProperties);

	FMcpSchemaProperty IncludeInherited;
	IncludeInherited.Type = TEXT("boolean");
	IncludeInherited.Description = TEXT("Include inherited properties from parent classes (default: false). Only applies when include_properties is true.");
	IncludeInherited.bRequired = false;
	Schema.Add(TEXT("include_inherited"), IncludeInherited);

	// List mode parameters
	FMcpSchemaProperty Limit;
	Limit.Type = TEXT("integer");
	Limit.Description = TEXT("Maximum number of results to return (default: 100)");
	Limit.bRequired = false;
	Schema.Add(TEXT("limit"), Limit);

	// World selection parameter
	FMcpSchemaProperty WorldParam;
	WorldParam.Type = TEXT("string");
	WorldParam.Description = TEXT("Target world: 'editor' (default), 'pie' (PIE only), or 'auto' (PIE if running, else editor)");
	WorldParam.bRequired = false;
	Schema.Add(TEXT("world"), WorldParam);

	return Schema;
}

TArray<FString> UQueryLevelTool::GetRequiredParams() const
{
	return {}; // No required params
}

FMcpToolResult UQueryLevelTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	if (!GEditor)
	{
		return FMcpToolResult::Error(TEXT("Editor not available"));
	}

	// Check for external level path - if provided, load and query that level
	FString LevelPath = GetStringArgOrDefault(Arguments, TEXT("level_path"), TEXT(""));
	if (!LevelPath.IsEmpty())
	{
		return QueryExternalLevel(LevelPath, Arguments);
	}

	// Get world parameter: 'editor' (default), 'pie', or 'auto'
	FString WorldParam = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor")).ToLower();

	UWorld* World = nullptr;

	if (WorldParam == TEXT("pie"))
	{
		// PIE 过渡期保护：避免在 PIE 启动/停止过程中访问正在创建/销毁的世界对象
		if (UPieSessionTool::IsPIETransitioning())
		{
			return FMcpToolResult::Error(TEXT("PIE is currently transitioning (starting or stopping). Please wait and try again."));
		}
		// PIE only - fail if not running
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::PIE && WorldContext.World())
			{
				World = WorldContext.World();
				break;
			}
		}
		if (!World)
		{
			return FMcpToolResult::Error(TEXT("No PIE session running. Use pie-session action:start first."));
		}
	}
	else if (WorldParam == TEXT("auto"))
	{
		// PIE 过渡期保护
		if (UPieSessionTool::IsPIETransitioning())
		{
			// auto 模式下 PIE 过渡中则回退到 editor world
			World = GEditor->GetEditorWorldContext().World();
			if (!World)
			{
				return FMcpToolResult::Error(TEXT("PIE is transitioning and no editor world available."));
			}
		}
		else
		{
		// Auto mode: prefer PIE if available, else editor
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::PIE && WorldContext.World())
			{
				World = WorldContext.World();
				break;
			}
		}
		if (!World)
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		if (!World)
		{
			return FMcpToolResult::Error(TEXT("No world loaded"));
		}
		} // end else (!IsPIETransitioning)
	}
	else
	{
		// Editor mode (default)
		World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return FMcpToolResult::Error(TEXT("No editor world available. Open a level first."));
		}
	}

	// Get all loaded levels (persistent + streaming sublevels)
	const TArray<ULevel*>& AllLevels = World->GetLevels();
	if (AllLevels.Num() == 0)
	{
		return FMcpToolResult::Error(TEXT("No levels found in world"));
	}

	// Verbose logging for debugging
	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: World='%s', WorldType=%d, NumLevels=%d"),
		*World->GetName(),
		(int32)World->WorldType,
		AllLevels.Num());

	// Count actors using TActorIterator (includes runtime-spawned actors)
	int32 TotalActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TotalActorCount++;
	}
	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: TActorIterator found %d total actors (including runtime-spawned)"), TotalActorCount);

	for (int32 i = 0; i < AllLevels.Num(); i++)
	{
		ULevel* Level = AllLevels[i];
		if (Level)
		{
			FString LevelName = Level->GetOuter() ? Level->GetOuter()->GetName() : TEXT("Unknown");
			UE_LOG(LogUEBridgeMCP, Log, TEXT("  Level[%d]: '%s', LevelActorCount=%d, bIsVisible=%d"),
				i, *LevelName, Level->Actors.Num(), Level->bIsVisible);
		}
	}

	// Check for detail mode (specific actor)
	FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"), TEXT(""));
	if (!ActorName.IsEmpty())
	{
		// Detail mode - return info for specific actor
		UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: detail mode for actor='%s'"), *ActorName);

		AActor* Actor = FMcpAssetModifier::FindActorByName(World, ActorName);
		if (!Actor)
		{
			return FMcpToolResult::Error(FString::Printf(
				TEXT("Actor '%s' not found in current world"), *ActorName));
		}

		// Default to basic info (false), user can request detailed properties/components
		bool bIncludeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_properties"), false);
		bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), false);
		bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
		bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), false);

		TSharedPtr<FJsonObject> Result = ActorToJson(Actor, bIncludeComponents, bIncludeTransform);

		// Add level and world type info
		if (ULevel* ActorLevel = Actor->GetLevel())
		{
			if (UObject* LevelOuter = ActorLevel->GetOuter())
			{
				Result->SetStringField(TEXT("level"), LevelOuter->GetName());
			}
		}

		FString WorldTypeStr;
		switch (World->WorldType)
		{
		case EWorldType::PIE: WorldTypeStr = TEXT("pie"); break;
		case EWorldType::Editor: WorldTypeStr = TEXT("editor"); break;
		case EWorldType::Game: WorldTypeStr = TEXT("game"); break;
		default: WorldTypeStr = TEXT("unknown"); break;
		}
		Result->SetStringField(TEXT("world_type"), WorldTypeStr);

		// If properties requested, add them
		if (bIncludeProperties)
		{
			TSharedPtr<FJsonObject> DetailedResult = ActorToDetailedJson(Actor, true, bIncludeComponents, bIncludeInherited);
			return FMcpToolResult::Json(DetailedResult);
		}

		return FMcpToolResult::Json(Result);
	}

	// List mode - return filtered list of actors
	FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"), TEXT(""));
	FString FolderFilter = GetStringArgOrDefault(Arguments, TEXT("folder_filter"), TEXT(""));
	FString TagFilter = GetStringArgOrDefault(Arguments, TEXT("tag_filter"), TEXT(""));
	bool bIncludeHidden = GetBoolArgOrDefault(Arguments, TEXT("include_hidden"), false);
	bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), false);
	bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
	int32 Limit = GetIntArgOrDefault(Arguments, TEXT("limit"), 100);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: list mode class='%s', folder='%s', tag='%s', limit=%d"),
		*ClassFilter, *FolderFilter, *TagFilter, Limit);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World->GetName());

	// Add world type info
	FString WorldTypeStr;
	switch (World->WorldType)
	{
	case EWorldType::PIE: WorldTypeStr = TEXT("pie"); break;
	case EWorldType::Editor: WorldTypeStr = TEXT("editor"); break;
	case EWorldType::Game: WorldTypeStr = TEXT("game"); break;
	default: WorldTypeStr = TEXT("unknown"); break;
	}
	Result->SetStringField(TEXT("world_type"), WorldTypeStr);

	// Add list of all loaded levels
	TArray<TSharedPtr<FJsonValue>> LevelsArray;
	for (ULevel* Level : AllLevels)
	{
		if (Level && Level->GetOuter())
		{
			LevelsArray.Add(MakeShareable(new FJsonValueString(Level->GetOuter()->GetName())));
		}
	}
	Result->SetArrayField(TEXT("levels"), LevelsArray);

	// Iterate ALL actors using TActorIterator (includes runtime-spawned actors)
	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 ProcessedCount = 0;
	bool bLimitReached = false;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		// Check limit
		if (ActorsArray.Num() >= Limit)
		{
			bLimitReached = true;
			break;
		}

		ProcessedCount++;

		// Apply hidden filter
		if (!bIncludeHidden && Actor->IsHidden())
		{
			continue;
		}

		// Apply class filter
		if (!ClassFilter.IsEmpty() && !MatchesClassFilter(Actor, ClassFilter))
		{
			continue;
		}

		// Apply folder filter
		if (!FolderFilter.IsEmpty() && !MatchesFolderFilter(Actor, FolderFilter))
		{
			continue;
		}

		// Apply tag filter
		if (!TagFilter.IsEmpty() && !MatchesTagFilter(Actor, TagFilter))
		{
			continue;
		}

		// Add actor to results
		TSharedPtr<FJsonObject> ActorJson = ActorToJson(Actor, bIncludeComponents, bIncludeTransform);
		if (ActorJson.IsValid())
		{
			// Add level info from actor's owning level
			if (ULevel* ActorLevel = Actor->GetLevel())
			{
				if (UObject* LevelOuter = ActorLevel->GetOuter())
				{
					ActorJson->SetStringField(TEXT("level"), LevelOuter->GetName());
				}
			}
			ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorJson)));
		}
	}

	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("actor_count"), ActorsArray.Num());
	Result->SetNumberField(TEXT("total_actors_processed"), ProcessedCount);
	Result->SetBoolField(TEXT("limit_reached"), bLimitReached);

	return FMcpToolResult::Json(Result);
}

// === List mode helpers ===

TSharedPtr<FJsonObject> UQueryLevelTool::ActorToJson(AActor* Actor, bool bIncludeComponents, bool bIncludeTransform) const
{
	if (!Actor)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ActorJson = MakeShareable(new FJsonObject);

	// Basic info
	ActorJson->SetStringField(TEXT("name"), Actor->GetName());
	ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
	ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	// Actor state
	ActorJson->SetBoolField(TEXT("is_hidden"), Actor->IsHidden());
	ActorJson->SetBoolField(TEXT("is_selected"), Actor->IsSelected());

	// Folder
	FName FolderPath = Actor->GetFolderPath();
	if (FolderPath != NAME_None)
	{
		ActorJson->SetStringField(TEXT("folder"), FolderPath.ToString());
	}

	// Tags
	if (Actor->Tags.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TagsArray;
		for (const FName& Tag : Actor->Tags)
		{
			TagsArray.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
		}
		ActorJson->SetArrayField(TEXT("tags"), TagsArray);
	}

	// Transform
	if (bIncludeTransform)
	{
		ActorJson->SetObjectField(TEXT("transform"), TransformToJson(Actor->GetActorTransform()));
	}

	// Components (basic info)
	if (bIncludeComponents)
	{
		TArray<TSharedPtr<FJsonValue>> ComponentsArray;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ComponentJson = MakeShareable(new FJsonObject);
			ComponentJson->SetStringField(TEXT("name"), Component->GetName());
			ComponentJson->SetStringField(TEXT("class"), Component->GetClass()->GetName());
			ComponentJson->SetBoolField(TEXT("is_active"), Component->IsActive());

			ComponentsArray.Add(MakeShareable(new FJsonValueObject(ComponentJson)));
		}

		ActorJson->SetArrayField(TEXT("components"), ComponentsArray);
		ActorJson->SetNumberField(TEXT("component_count"), ComponentsArray.Num());
	}

	return ActorJson;
}

bool UQueryLevelTool::MatchesClassFilter(AActor* Actor, const FString& Filter) const
{
	if (!Actor || Filter.IsEmpty())
	{
		return true;
	}

	FString ClassName = Actor->GetClass()->GetName();
	return MatchesWildcard(ClassName, Filter);
}

bool UQueryLevelTool::MatchesFolderFilter(AActor* Actor, const FString& Filter) const
{
	if (!Actor || Filter.IsEmpty())
	{
		return true;
	}

	FName FolderPath = Actor->GetFolderPath();
	if (FolderPath == NAME_None)
	{
		return false;
	}

	FString FolderString = FolderPath.ToString();
	return MatchesWildcard(FolderString, Filter);
}

bool UQueryLevelTool::MatchesTagFilter(AActor* Actor, const FString& Filter) const
{
	if (!Actor || Filter.IsEmpty())
	{
		return true;
	}

	for (const FName& Tag : Actor->Tags)
	{
		if (MatchesWildcard(Tag.ToString(), Filter))
		{
			return true;
		}
	}

	return false;
}

bool UQueryLevelTool::MatchesWildcard(const FString& Name, const FString& Pattern) const
{
	if (Pattern.IsEmpty())
	{
		return true;
	}

	// Simple wildcard matching
	if (Pattern.Contains(TEXT("*")))
	{
		if (Pattern.StartsWith(TEXT("*")) && Pattern.EndsWith(TEXT("*")))
		{
			// *substring*
			FString Substring = Pattern.Mid(1, Pattern.Len() - 2);
			return Name.Contains(Substring);
		}
		else if (Pattern.StartsWith(TEXT("*")))
		{
			// *suffix
			FString Suffix = Pattern.Mid(1);
			return Name.EndsWith(Suffix);
		}
		else if (Pattern.EndsWith(TEXT("*")))
		{
			// prefix*
			FString Prefix = Pattern.Left(Pattern.Len() - 1);
			return Name.StartsWith(Prefix);
		}
	}

	// Exact match
	return Name.Equals(Pattern, ESearchCase::IgnoreCase);
}

// === Detail mode helpers ===

TSharedPtr<FJsonObject> UQueryLevelTool::ActorToDetailedJson(AActor* Actor, bool bIncludeProperties, bool bIncludeComponents, bool bIncludeInherited) const
{
	if (!Actor)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ActorJson = MakeShareable(new FJsonObject);

	// Basic info
	ActorJson->SetStringField(TEXT("name"), Actor->GetName());
	ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
	ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	// Level info (which level contains this actor)
	if (ULevel* ActorLevel = Actor->GetLevel())
	{
		if (UObject* LevelOuter = ActorLevel->GetOuter())
		{
			ActorJson->SetStringField(TEXT("level"), LevelOuter->GetName());
		}
	}

	// Parent class hierarchy
	TArray<TSharedPtr<FJsonValue>> ParentClassesArray;
	UClass* CurrentClass = Actor->GetClass()->GetSuperClass();
	while (CurrentClass && CurrentClass != UObject::StaticClass())
	{
		ParentClassesArray.Add(MakeShareable(new FJsonValueString(CurrentClass->GetName())));
		CurrentClass = CurrentClass->GetSuperClass();
	}
	if (ParentClassesArray.Num() > 0)
	{
		ActorJson->SetArrayField(TEXT("parent_classes"), ParentClassesArray);
	}

	// Actor state
	ActorJson->SetBoolField(TEXT("is_hidden"), Actor->IsHidden());
	ActorJson->SetBoolField(TEXT("is_selected"), Actor->IsSelected());
	ActorJson->SetBoolField(TEXT("is_editor_only"), Actor->IsEditorOnly());

	// Folder
	FName FolderPath = Actor->GetFolderPath();
	if (FolderPath != NAME_None)
	{
		ActorJson->SetStringField(TEXT("folder"), FolderPath.ToString());
	}

	// Tags
	if (Actor->Tags.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TagsArray;
		for (const FName& Tag : Actor->Tags)
		{
			TagsArray.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
		}
		ActorJson->SetArrayField(TEXT("tags"), TagsArray);
	}

	// Transform
	ActorJson->SetObjectField(TEXT("transform"), TransformToJson(Actor->GetActorTransform()));

	// Properties
	if (bIncludeProperties)
	{
		TArray<TSharedPtr<FJsonValue>> PropertiesArray;

		EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited
			? EFieldIteratorFlags::IncludeSuper
			: EFieldIteratorFlags::ExcludeSuper;

		for (TFieldIterator<FProperty> PropIt(Actor->GetClass(), SuperFlags); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (!Property)
			{
				continue;
			}

			// Skip some internal properties
			FString PropertyName = Property->GetName();
			if (PropertyName.StartsWith(TEXT("b")) && PropertyName.Contains(TEXT("Internal")))
			{
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Actor);
			if (!ValuePtr)
			{
				continue;
			}

			TSharedPtr<FJsonObject> PropertyJson = PropertyToJson(Property, ValuePtr, Actor);
			if (PropertyJson.IsValid())
			{
				PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropertyJson)));
			}
		}

		ActorJson->SetArrayField(TEXT("properties"), PropertiesArray);
		ActorJson->SetNumberField(TEXT("property_count"), PropertiesArray.Num());
	}

	// Components (detailed)
	if (bIncludeComponents)
	{
		TArray<TSharedPtr<FJsonValue>> ComponentsArray;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ComponentJson = ComponentToDetailedJson(Component, bIncludeProperties, bIncludeInherited);
			if (ComponentJson.IsValid())
			{
				ComponentsArray.Add(MakeShareable(new FJsonValueObject(ComponentJson)));
			}
		}

		ActorJson->SetArrayField(TEXT("components"), ComponentsArray);
		ActorJson->SetNumberField(TEXT("component_count"), ComponentsArray.Num());
	}

	return ActorJson;
}

TSharedPtr<FJsonObject> UQueryLevelTool::ComponentToDetailedJson(UActorComponent* Component, bool bIncludeProperties, bool bIncludeInherited) const
{
	if (!Component)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ComponentJson = MakeShareable(new FJsonObject);

	// Basic info
	ComponentJson->SetStringField(TEXT("name"), Component->GetName());
	ComponentJson->SetStringField(TEXT("class"), Component->GetClass()->GetName());
	ComponentJson->SetBoolField(TEXT("is_active"), Component->IsActive());
	ComponentJson->SetBoolField(TEXT("is_editor_only"), Component->IsEditorOnly());

	// Scene component specific
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		ComponentJson->SetBoolField(TEXT("is_scene_component"), true);

		// Relative transform
		ComponentJson->SetObjectField(TEXT("relative_transform"), TransformToJson(SceneComponent->GetRelativeTransform()));

		// World transform
		ComponentJson->SetObjectField(TEXT("world_transform"), TransformToJson(SceneComponent->GetComponentTransform()));

		// Mobility
		FString Mobility;
		switch (SceneComponent->Mobility)
		{
		case EComponentMobility::Static:
			Mobility = TEXT("Static");
			break;
		case EComponentMobility::Stationary:
			Mobility = TEXT("Stationary");
			break;
		case EComponentMobility::Movable:
			Mobility = TEXT("Movable");
			break;
		default:
			Mobility = TEXT("Unknown");
		}
		ComponentJson->SetStringField(TEXT("mobility"), Mobility);

		// Parent component
		if (SceneComponent->GetAttachParent())
		{
			ComponentJson->SetStringField(TEXT("parent_component"), SceneComponent->GetAttachParent()->GetName());
		}

		// Child components
		TArray<USceneComponent*> ChildComponents = SceneComponent->GetAttachChildren();
		if (ChildComponents.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArray;
			for (USceneComponent* Child : ChildComponents)
			{
				if (Child)
				{
					ChildrenArray.Add(MakeShareable(new FJsonValueString(Child->GetName())));
				}
			}
			ComponentJson->SetArrayField(TEXT("child_components"), ChildrenArray);
		}
	}
	else
	{
		ComponentJson->SetBoolField(TEXT("is_scene_component"), false);
	}

	// Properties
	if (bIncludeProperties)
	{
		TArray<TSharedPtr<FJsonValue>> PropertiesArray;

		EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited
			? EFieldIteratorFlags::IncludeSuper
			: EFieldIteratorFlags::ExcludeSuper;

		for (TFieldIterator<FProperty> PropIt(Component->GetClass(), SuperFlags); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (!Property)
			{
				continue;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Component);
			if (!ValuePtr)
			{
				continue;
			}

			TSharedPtr<FJsonObject> PropertyJson = PropertyToJson(Property, ValuePtr, Component);
			if (PropertyJson.IsValid())
			{
				PropertiesArray.Add(MakeShareable(new FJsonValueObject(PropertyJson)));
			}
		}

		ComponentJson->SetArrayField(TEXT("properties"), PropertiesArray);
		ComponentJson->SetNumberField(TEXT("property_count"), PropertiesArray.Num());
	}

	return ComponentJson;
}

TSharedPtr<FJsonObject> UQueryLevelTool::PropertyToJson(FProperty* Property, void* ValuePtr, UObject* Owner) const
{
	if (!Property || !ValuePtr)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> PropertyJson = MakeShareable(new FJsonObject);

	// Basic info
	PropertyJson->SetStringField(TEXT("name"), Property->GetName());
	PropertyJson->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

	// Category
	FString Category = Property->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty())
	{
		PropertyJson->SetStringField(TEXT("category"), Category);
	}

	// Export value as string
	FString Value;
	Property->ExportText_Direct(Value, ValuePtr, ValuePtr, Owner, PPF_None);
	PropertyJson->SetStringField(TEXT("value"), Value);

	return PropertyJson;
}

FString UQueryLevelTool::GetPropertyTypeString(FProperty* Property) const
{
	if (!Property)
	{
		return TEXT("unknown");
	}

	// Check for specific property types
	if (Property->IsA<FBoolProperty>())
	{
		return TEXT("bool");
	}
	else if (Property->IsA<FIntProperty>())
	{
		return TEXT("int32");
	}
	else if (Property->IsA<FFloatProperty>())
	{
		return TEXT("float");
	}
	else if (Property->IsA<FNameProperty>())
	{
		return TEXT("FName");
	}
	else if (Property->IsA<FStrProperty>())
	{
		return TEXT("FString");
	}
	else if (Property->IsA<FTextProperty>())
	{
		return TEXT("FText");
	}
	else if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
	{
		if (ObjectProp->PropertyClass)
		{
			return FString::Printf(TEXT("TObjectPtr<%s>"), *ObjectProp->PropertyClass->GetName());
		}
		return TEXT("TObjectPtr<UObject>");
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct)
		{
			return StructProp->Struct->GetName();
		}
		return TEXT("struct");
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FString InnerType = GetPropertyTypeString(ArrayProp->Inner);
		return FString::Printf(TEXT("TArray<%s>"), *InnerType);
	}

	// Fallback
	return Property->GetClass()->GetName();
}

// === Shared helpers ===

TSharedPtr<FJsonObject> UQueryLevelTool::TransformToJson(const FTransform& Transform) const
{
	TSharedPtr<FJsonObject> TransformJson = MakeShareable(new FJsonObject);

	// Location
	TSharedPtr<FJsonObject> LocationJson = MakeShareable(new FJsonObject);
	LocationJson->SetNumberField(TEXT("x"), Transform.GetLocation().X);
	LocationJson->SetNumberField(TEXT("y"), Transform.GetLocation().Y);
	LocationJson->SetNumberField(TEXT("z"), Transform.GetLocation().Z);
	TransformJson->SetObjectField(TEXT("location"), LocationJson);

	// Rotation
	TSharedPtr<FJsonObject> RotationJson = MakeShareable(new FJsonObject);
	FRotator Rotator = Transform.Rotator();
	RotationJson->SetNumberField(TEXT("pitch"), Rotator.Pitch);
	RotationJson->SetNumberField(TEXT("yaw"), Rotator.Yaw);
	RotationJson->SetNumberField(TEXT("roll"), Rotator.Roll);
	TransformJson->SetObjectField(TEXT("rotation"), RotationJson);

	// Scale
	TSharedPtr<FJsonObject> ScaleJson = MakeShareable(new FJsonObject);
	ScaleJson->SetNumberField(TEXT("x"), Transform.GetScale3D().X);
	ScaleJson->SetNumberField(TEXT("y"), Transform.GetScale3D().Y);
	ScaleJson->SetNumberField(TEXT("z"), Transform.GetScale3D().Z);
	TransformJson->SetObjectField(TEXT("scale"), ScaleJson);

	return TransformJson;
}

// === External level loading ===

FMcpToolResult UQueryLevelTool::QueryExternalLevel(const FString& LevelPath, const TSharedPtr<FJsonObject>& Arguments) const
{
	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: Loading external level '%s'"), *LevelPath);

	// Load the level package without opening it in the editor
	UWorld* LoadedWorld = Cast<UWorld>(StaticLoadObject(UWorld::StaticClass(), nullptr, *LevelPath, nullptr, LOAD_NoWarn | LOAD_Quiet));

	if (!LoadedWorld)
	{
		return FMcpToolResult::Error(FString::Printf(
			TEXT("Failed to load level: %s. Ensure the path is correct (e.g., '/Game/Maps/Level2')."), *LevelPath));
	}

	// Get parameters
	FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"), TEXT(""));
	FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"), TEXT(""));
	FString FolderFilter = GetStringArgOrDefault(Arguments, TEXT("folder_filter"), TEXT(""));
	FString TagFilter = GetStringArgOrDefault(Arguments, TEXT("tag_filter"), TEXT(""));
	bool bIncludeHidden = GetBoolArgOrDefault(Arguments, TEXT("include_hidden"), false);
	bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), false);
	bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
	bool bIncludeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_properties"), false);
	bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), false);
	int32 Limit = GetIntArgOrDefault(Arguments, TEXT("limit"), 100);

	// Get the persistent level
	ULevel* PersistentLevel = LoadedWorld->PersistentLevel;
	if (!PersistentLevel)
	{
		return FMcpToolResult::Error(FString::Printf(
			TEXT("Level '%s' has no persistent level data."), *LevelPath));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: External level '%s' has %d actors"),
		*LevelPath, PersistentLevel->Actors.Num());

	// Detail mode - find specific actor
	if (!ActorName.IsEmpty())
	{
		for (AActor* Actor : PersistentLevel->Actors)
		{
			if (!Actor)
			{
				continue;
			}

			if (MatchesWildcard(Actor->GetName(), ActorName) ||
				MatchesWildcard(Actor->GetActorLabel(), ActorName))
			{
				UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: Found actor '%s' in external level"), *Actor->GetName());

				TSharedPtr<FJsonObject> Result;
				if (bIncludeProperties)
				{
					Result = ActorToDetailedJson(Actor, true, bIncludeComponents, bIncludeInherited);
				}
				else
				{
					Result = ActorToJson(Actor, bIncludeComponents, bIncludeTransform);
				}

				Result->SetStringField(TEXT("level_path"), LevelPath);
				Result->SetStringField(TEXT("level"), LoadedWorld->GetName());
				return FMcpToolResult::Json(Result);
			}
		}

		return FMcpToolResult::Error(FString::Printf(
			TEXT("Actor '%s' not found in level '%s'"), *ActorName, *LevelPath));
	}

	// List mode - return filtered actors
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("level_path"), LevelPath);
	Result->SetStringField(TEXT("level_name"), LoadedWorld->GetName());

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 ProcessedCount = 0;
	bool bLimitReached = false;

	for (AActor* Actor : PersistentLevel->Actors)
	{
		if (!Actor)
		{
			continue;
		}

		// Check limit
		if (ActorsArray.Num() >= Limit)
		{
			bLimitReached = true;
			break;
		}

		ProcessedCount++;

		// Apply hidden filter
		if (!bIncludeHidden && Actor->IsHidden())
		{
			continue;
		}

		// Apply class filter
		if (!ClassFilter.IsEmpty() && !MatchesClassFilter(Actor, ClassFilter))
		{
			continue;
		}

		// Apply folder filter
		if (!FolderFilter.IsEmpty() && !MatchesFolderFilter(Actor, FolderFilter))
		{
			continue;
		}

		// Apply tag filter
		if (!TagFilter.IsEmpty() && !MatchesTagFilter(Actor, TagFilter))
		{
			continue;
		}

		// Add actor to results
		TSharedPtr<FJsonObject> ActorJson = ActorToJson(Actor, bIncludeComponents, bIncludeTransform);
		if (ActorJson.IsValid())
		{
			ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorJson)));
		}
	}

	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("actor_count"), ActorsArray.Num());
	Result->SetNumberField(TEXT("total_actors_in_level"), PersistentLevel->Actors.Num());
	Result->SetNumberField(TEXT("total_actors_processed"), ProcessedCount);
	Result->SetBoolField(TEXT("limit_reached"), bLimitReached);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: External level query complete - %d actors returned"),
		ActorsArray.Num());

	// 卸载临时加载的外部关卡，避免内存泄漏
	if (LoadedWorld && !LoadedWorld->HasAnyFlags(RF_Standalone))
	{
		UE_LOG(LogUEBridgeMCP, Log, TEXT("query-level: Unloading temporary external level '%s'"), *LevelPath);
		// 清除对 Actor 的引用后，重置包的脏标记并卸载
		UPackage* LevelPackage = LoadedWorld->GetOutermost();
		if (LevelPackage)
		{
			LevelPackage->ClearDirtyFlag();
			// 标记为可回收，让 GC 在下次运行时清理
			LoadedWorld->ClearFlags(RF_Standalone);
			LoadedWorld->MarkAsGarbage();
		}
	}

	return FMcpToolResult::Json(Result);
}
