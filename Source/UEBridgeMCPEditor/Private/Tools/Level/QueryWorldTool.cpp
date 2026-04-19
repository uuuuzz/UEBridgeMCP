// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Level/QueryWorldTool.h"
#include "Utils/McpAssetModifier.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Engine/Selection.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace QueryWorldToolPrivate
{
	TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& Vector)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Add(MakeShareable(new FJsonValueNumber(Vector.X)));
		Result.Add(MakeShareable(new FJsonValueNumber(Vector.Y)));
		Result.Add(MakeShareable(new FJsonValueNumber(Vector.Z)));
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> RotatorToArray(const FRotator& Rotator)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Add(MakeShareable(new FJsonValueNumber(Rotator.Pitch)));
		Result.Add(MakeShareable(new FJsonValueNumber(Rotator.Yaw)));
		Result.Add(MakeShareable(new FJsonValueNumber(Rotator.Roll)));
		return Result;
	}

	TSharedPtr<FJsonObject> TransformToJson(const FTransform& Transform)
	{
		TSharedPtr<FJsonObject> TransformObject = MakeShareable(new FJsonObject);
		TransformObject->SetArrayField(TEXT("location"), VectorToArray(Transform.GetLocation()));
		TransformObject->SetArrayField(TEXT("rotation"), RotatorToArray(Transform.Rotator()));
		TransformObject->SetArrayField(TEXT("scale"), VectorToArray(Transform.GetScale3D()));
		return TransformObject;
	}

	bool MatchesActorName(AActor* Actor, const FString& ActorName)
	{
		if (!Actor || ActorName.IsEmpty())
		{
			return false;
		}

		const FString Name = Actor->GetName();
		const FString Label = Actor->GetActorLabel();
		const FString NameOrLabel = Actor->GetActorNameOrLabel();
		const bool bHasWildcard = ActorName.Contains(TEXT("*")) || ActorName.Contains(TEXT("?"));

		if (bHasWildcard)
		{
			return Name.MatchesWildcard(ActorName, ESearchCase::IgnoreCase)
				|| Label.MatchesWildcard(ActorName, ESearchCase::IgnoreCase)
				|| NameOrLabel.MatchesWildcard(ActorName, ESearchCase::IgnoreCase);
		}

		return Name.Equals(ActorName, ESearchCase::IgnoreCase)
			|| Label.Equals(ActorName, ESearchCase::IgnoreCase)
			|| NameOrLabel.Equals(ActorName, ESearchCase::IgnoreCase);
	}

	bool MatchesTagFilter(AActor* Actor, const FString& TagFilter)
	{
		if (!Actor || TagFilter.IsEmpty())
		{
			return true;
		}

		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().MatchesWildcard(TagFilter, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	bool MatchesFolderFilter(AActor* Actor, const FString& FolderFilter)
	{
		if (!Actor || FolderFilter.IsEmpty())
		{
			return true;
		}

		const FString FolderPath = Actor->GetFolderPath().ToString();
		if (FolderFilter.Contains(TEXT("*")) || FolderFilter.Contains(TEXT("?")))
		{
			return FolderPath.MatchesWildcard(FolderFilter, ESearchCase::IgnoreCase);
		}

		return FolderPath.StartsWith(FolderFilter, ESearchCase::IgnoreCase);
	}

	FString GetPropertyTypeString(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("unknown");
		}

		if (Property->IsA<FBoolProperty>())
		{
			return TEXT("bool");
		}
		if (Property->IsA<FIntProperty>())
		{
			return TEXT("int32");
		}
		if (Property->IsA<FInt64Property>())
		{
			return TEXT("int64");
		}
		if (Property->IsA<FFloatProperty>())
		{
			return TEXT("float");
		}
		if (Property->IsA<FDoubleProperty>())
		{
			return TEXT("double");
		}
		if (Property->IsA<FNameProperty>())
		{
			return TEXT("FName");
		}
		if (Property->IsA<FStrProperty>())
		{
			return TEXT("FString");
		}
		if (Property->IsA<FTextProperty>())
		{
			return TEXT("FText");
		}

		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		if (ObjectProperty && ObjectProperty->PropertyClass)
		{
			return FString::Printf(TEXT("TObjectPtr<%s>"), *ObjectProperty->PropertyClass->GetName());
		}

		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct)
		{
			return StructProperty->Struct->GetName();
		}

		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
		if (ArrayProperty)
		{
			return FString::Printf(TEXT("TArray<%s>"), *GetPropertyTypeString(ArrayProperty->Inner));
		}

		return Property->GetClass()->GetName();
	}

	TSharedPtr<FJsonObject> PropertyToJson(FProperty* Property, void* ValuePointer, UObject* Owner)
	{
		if (!Property || !ValuePointer)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> PropertyObject = MakeShareable(new FJsonObject);
		PropertyObject->SetStringField(TEXT("name"), Property->GetName());
		PropertyObject->SetStringField(TEXT("type"), GetPropertyTypeString(Property));

		const FString Category = Property->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			PropertyObject->SetStringField(TEXT("category"), Category);
		}

		FString Value;
		Property->ExportText_Direct(Value, ValuePointer, ValuePointer, Owner, PPF_None);
		PropertyObject->SetStringField(TEXT("value"), Value);
		return PropertyObject;
	}

	TSharedPtr<FJsonObject> SerializeComponent(UActorComponent* Component, bool bIncludeProperties, bool bIncludeInherited)
	{
		if (!Component)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> ComponentObject = MakeShareable(new FJsonObject);
		ComponentObject->SetStringField(TEXT("name"), Component->GetName());
		ComponentObject->SetStringField(TEXT("class"), Component->GetClass()->GetName());
		ComponentObject->SetBoolField(TEXT("is_active"), Component->IsActive());
		ComponentObject->SetBoolField(TEXT("is_editor_only"), Component->IsEditorOnly());

		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (SceneComponent)
		{
			ComponentObject->SetObjectField(TEXT("relative_transform"), TransformToJson(SceneComponent->GetRelativeTransform()));
			ComponentObject->SetObjectField(TEXT("world_transform"), TransformToJson(SceneComponent->GetComponentTransform()));
			if (SceneComponent->GetAttachParent())
			{
				ComponentObject->SetStringField(TEXT("parent_component"), SceneComponent->GetAttachParent()->GetName());
			}
		}

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> PropertyArray;
			const EFieldIteratorFlags::SuperClassFlags SuperClassFlags = bIncludeInherited
				? EFieldIteratorFlags::IncludeSuper
				: EFieldIteratorFlags::ExcludeSuper;

			for (TFieldIterator<FProperty> PropertyIterator(Component->GetClass(), SuperClassFlags); PropertyIterator; ++PropertyIterator)
			{
				FProperty* Property = *PropertyIterator;
				if (!Property)
				{
					continue;
				}

				void* ValuePointer = Property->ContainerPtrToValuePtr<void>(Component);
				if (!ValuePointer)
				{
					continue;
				}

				TSharedPtr<FJsonObject> PropertyObject = PropertyToJson(Property, ValuePointer, Component);
				if (PropertyObject.IsValid())
				{
					PropertyArray.Add(MakeShareable(new FJsonValueObject(PropertyObject)));
				}
			}

			ComponentObject->SetArrayField(TEXT("properties"), PropertyArray);
			ComponentObject->SetNumberField(TEXT("property_count"), PropertyArray.Num());
		}

		return ComponentObject;
	}

	TSharedPtr<FJsonObject> SerializeActor(
		AActor* Actor,
		bool bIncludeComponents,
		bool bIncludeTransform,
		bool bIncludeProperties,
		bool bIncludeTags,
		bool bIncludeBounds,
		bool bIncludeInherited,
		const FString& ComponentFilter)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> ActorObject = MakeShareable(new FJsonObject);
		ActorObject->SetStringField(TEXT("name"), Actor->GetActorNameOrLabel());
		ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorObject->SetStringField(TEXT("path_name"), Actor->GetPathName());
		ActorObject->SetBoolField(TEXT("is_hidden"), Actor->IsHidden());
		ActorObject->SetBoolField(TEXT("is_selected"), Actor->IsSelected());

		const FString FolderPath = Actor->GetFolderPath().ToString();
		if (!FolderPath.IsEmpty())
		{
			ActorObject->SetStringField(TEXT("folder"), FolderPath);
		}

		if (bIncludeTransform)
		{
			ActorObject->SetObjectField(TEXT("transform"), TransformToJson(Actor->GetActorTransform()));
		}

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> PropertyArray;
			const EFieldIteratorFlags::SuperClassFlags SuperClassFlags = bIncludeInherited
				? EFieldIteratorFlags::IncludeSuper
				: EFieldIteratorFlags::ExcludeSuper;

			for (TFieldIterator<FProperty> PropertyIterator(Actor->GetClass(), SuperClassFlags); PropertyIterator; ++PropertyIterator)
			{
				FProperty* Property = *PropertyIterator;
				if (!Property)
				{
					continue;
				}

				void* ValuePointer = Property->ContainerPtrToValuePtr<void>(Actor);
				if (!ValuePointer)
				{
					continue;
				}

				TSharedPtr<FJsonObject> PropertyObject = PropertyToJson(Property, ValuePointer, Actor);
				if (PropertyObject.IsValid())
				{
					PropertyArray.Add(MakeShareable(new FJsonValueObject(PropertyObject)));
				}
			}

			ActorObject->SetArrayField(TEXT("properties"), PropertyArray);
			ActorObject->SetNumberField(TEXT("property_count"), PropertyArray.Num());
		}

		if (bIncludeComponents)
		{
			TArray<TSharedPtr<FJsonValue>> ComponentArray;
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}

				if (!ComponentFilter.IsEmpty() && !Component->GetName().Equals(ComponentFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}

				TSharedPtr<FJsonObject> ComponentObject = SerializeComponent(Component, bIncludeProperties, bIncludeInherited);
				if (ComponentObject.IsValid())
				{
					ComponentArray.Add(MakeShareable(new FJsonValueObject(ComponentObject)));
				}
			}

			ActorObject->SetArrayField(TEXT("components"), ComponentArray);
			ActorObject->SetNumberField(TEXT("component_count"), ComponentArray.Num());
		}

		if (bIncludeTags)
		{
			TArray<TSharedPtr<FJsonValue>> TagArray;
			for (const FName& Tag : Actor->Tags)
			{
				TagArray.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
			}
			ActorObject->SetArrayField(TEXT("tags"), TagArray);
		}

		if (bIncludeBounds)
		{
			const FBox Bounds = Actor->GetComponentsBoundingBox();
			if (Bounds.IsValid)
			{
				TSharedPtr<FJsonObject> BoundsObject = MakeShareable(new FJsonObject);
				BoundsObject->SetArrayField(TEXT("min"), VectorToArray(Bounds.Min));
				BoundsObject->SetArrayField(TEXT("max"), VectorToArray(Bounds.Max));
				ActorObject->SetObjectField(TEXT("bounds"), BoundsObject);
			}
		}

		return ActorObject;
	}
}

FString UQueryWorldTool::GetToolDescription() const
{
	return TEXT("Query editor or PIE world state with verification-oriented detail modes for actors and components. "
		"Supports filters for actor name, class, tag, folder, selection, properties, and bounds.");
}

TMap<FString, FMcpSchemaProperty> UQueryWorldTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(
		TEXT("World to query: 'editor', 'pie', or 'auto' (default: auto)"),
		{TEXT("editor"), TEXT("pie"), TEXT("auto")}));

	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("If specified, return single-actor detail mode. Matches actor name or label and supports wildcards.")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Component detail mode under the resolved actor. If actor_name is omitted, query-world uses the single selected editor actor.")));
	Schema.Add(TEXT("class_filter"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Filter actors by class name (wildcards allowed)")));
	Schema.Add(TEXT("tag_filter"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Filter actors by tag (wildcards allowed)")));
	Schema.Add(TEXT("folder_filter"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Filter by World Outliner folder path (prefix or wildcard)")));

	Schema.Add(TEXT("include_components"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include component list")));
	Schema.Add(TEXT("include_transform"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actor transforms")));
	Schema.Add(TEXT("include_properties"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include reflected properties")));
	Schema.Add(TEXT("include_inherited"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include inherited reflected properties when include_properties is true")));
	Schema.Add(TEXT("include_tags"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include actor tags")));
	Schema.Add(TEXT("include_bounds"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include bounds data")));
	Schema.Add(TEXT("only_selected"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Only selected actors (editor world)")));
	Schema.Add(TEXT("limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Max results (default 100)")));

	return Schema;
}

TArray<FString> UQueryWorldTool::GetRequiredParams() const
{
	return {};
}

FMcpToolResult UQueryWorldTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("auto"));
	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"));
	const FString TagFilter = GetStringArgOrDefault(Arguments, TEXT("tag_filter"));
	const FString FolderFilter = GetStringArgOrDefault(Arguments, TEXT("folder_filter"));

	const bool bIncludeComponents = GetBoolArgOrDefault(Arguments, TEXT("include_components"), false);
	const bool bIncludeTransform = GetBoolArgOrDefault(Arguments, TEXT("include_transform"), true);
	const bool bIncludeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_properties"), false);
	const bool bIncludeInherited = GetBoolArgOrDefault(Arguments, TEXT("include_inherited"), false);
	const bool bIncludeTags = GetBoolArgOrDefault(Arguments, TEXT("include_tags"), false);
	const bool bIncludeBounds = GetBoolArgOrDefault(Arguments, TEXT("include_bounds"), false);
	const bool bOnlySelected = GetBoolArgOrDefault(Arguments, TEXT("only_selected"), false);
	const int32 Limit = FMath::Max(1, GetIntArgOrDefault(Arguments, TEXT("limit"), 100));

	bool bIsPIE = false;
	UWorld* World = FMcpAssetModifier::ResolveWorld(WorldType, bIsPIE);
	if (!World)
	{
		const FString ErrorMessage = WorldType.Equals(TEXT("pie"), ESearchCase::IgnoreCase)
			? TEXT("PIE is not running")
			: TEXT("No world available");
		const FString ErrorCode = WorldType.Equals(TEXT("pie"), ESearchCase::IgnoreCase)
			? TEXT("UEBMCP_PIE_NOT_RUNNING")
			: TEXT("UEBMCP_WORLD_NOT_AVAILABLE");
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage);
	}

	TSharedPtr<FJsonObject> WorldInfo = MakeShareable(new FJsonObject);
	WorldInfo->SetStringField(TEXT("type"), bIsPIE ? TEXT("pie") : TEXT("editor"));
	WorldInfo->SetStringField(TEXT("map_name"), World->GetMapName());
	WorldInfo->SetBoolField(TEXT("is_pie_running"), GEditor && GEditor->PlayWorld != nullptr);

	FString ResolvedActorName = ActorName;
	if (ResolvedActorName.IsEmpty() && !ComponentName.IsEmpty())
	{
		if (!GEditor || bIsPIE)
		{
			return FMcpToolResult::StructuredError(
				TEXT("UEBMCP_UNSUPPORTED_COMBINATION"),
				TEXT("'component_name' without 'actor_name' is only supported for a single selected actor in the editor world"));
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors || SelectedActors->Num() != 1)
		{
			return FMcpToolResult::StructuredError(
				TEXT("UEBMCP_UNSUPPORTED_COMBINATION"),
				TEXT("'component_name' without 'actor_name' requires exactly one selected actor in the editor world"));
		}

		AActor* SelectedActor = Cast<AActor>(SelectedActors->GetSelectedObject(0));
		if (!SelectedActor)
		{
			return FMcpToolResult::StructuredError(
				TEXT("UEBMCP_ACTOR_NOT_FOUND"),
				TEXT("No valid selected actor was found for component detail mode"));
		}

		ResolvedActorName = SelectedActor->GetActorNameOrLabel();
	}

	if (!ResolvedActorName.IsEmpty())
	{
		TArray<AActor*> MatchingActors;
		for (TActorIterator<AActor> Iterator(World); Iterator; ++Iterator)
		{
			AActor* Actor = *Iterator;
			if (QueryWorldToolPrivate::MatchesActorName(Actor, ResolvedActorName))
			{
				MatchingActors.Add(Actor);
			}
		}

		if (MatchingActors.Num() == 0)
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("actor_name"), ResolvedActorName);
			Details->SetStringField(TEXT("world"), WorldInfo->GetStringField(TEXT("type")));
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_NOT_FOUND"), TEXT("Requested actor was not found"), Details);
		}

		if (MatchingActors.Num() > 1)
		{
			TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
			Details->SetStringField(TEXT("actor_name"), ResolvedActorName);
			TArray<TSharedPtr<FJsonValue>> MatchNames;
			for (AActor* MatchingActor : MatchingActors)
			{
				MatchNames.Add(MakeShareable(new FJsonValueString(MatchingActor->GetActorNameOrLabel())));
			}
			Details->SetArrayField(TEXT("matches"), MatchNames);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ACTOR_MULTIPLE_MATCHES"), TEXT("Multiple actors matched the requested actor_name"), Details);
		}

		AActor* MatchedActor = MatchingActors[0];
		if (!ComponentName.IsEmpty())
		{
			UActorComponent* MatchedComponent = FMcpAssetModifier::FindComponentByName(MatchedActor, ComponentName);
			if (!MatchedComponent)
			{
				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("actor_name"), MatchedActor->GetActorNameOrLabel());
				Details->SetStringField(TEXT("component_name"), ComponentName);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), TEXT("Requested component was not found on the resolved actor"), Details);
			}
		}

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), TEXT("query-world"));
		Response->SetBoolField(TEXT("success"), true);
		Response->SetObjectField(TEXT("world"), WorldInfo);

		TArray<TSharedPtr<FJsonValue>> ActorsArray;
		TSharedPtr<FJsonObject> ActorObject = QueryWorldToolPrivate::SerializeActor(
			MatchedActor,
			bIncludeComponents || !ComponentName.IsEmpty(),
			bIncludeTransform,
			bIncludeProperties,
			bIncludeTags,
			bIncludeBounds,
			bIncludeInherited,
			ComponentName);
		ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObject)));
		Response->SetArrayField(TEXT("actors"), ActorsArray);
		Response->SetNumberField(TEXT("actor_count"), 1);
		Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
		Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());

		if (!ComponentName.IsEmpty())
		{
			Response->SetStringField(TEXT("requested_component"), ComponentName);
		}

		return FMcpToolResult::StructuredJson(Response);
	}

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 Count = 0;
	bool bLimitReached = false;

	for (TActorIterator<AActor> Iterator(World); Iterator; ++Iterator)
	{
		AActor* Actor = *Iterator;
		if (!Actor)
		{
			continue;
		}

		if (bOnlySelected && GEditor)
		{
			USelection* SelectedActors = GEditor->GetSelectedActors();
			if (SelectedActors && !SelectedActors->IsSelected(Actor))
			{
				continue;
			}
		}

		if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().MatchesWildcard(ClassFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (!QueryWorldToolPrivate::MatchesTagFilter(Actor, TagFilter))
		{
			continue;
		}

		if (!QueryWorldToolPrivate::MatchesFolderFilter(Actor, FolderFilter))
		{
			continue;
		}

		ActorsArray.Add(MakeShareable(new FJsonValueObject(QueryWorldToolPrivate::SerializeActor(
			Actor,
			bIncludeComponents,
			bIncludeTransform,
			bIncludeProperties,
			bIncludeTags,
			bIncludeBounds,
			bIncludeInherited,
			TEXT("")))));

		Count++;
		if (Count >= Limit)
		{
			bLimitReached = true;
			break;
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("query-world"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetObjectField(TEXT("world"), WorldInfo);
	Response->SetArrayField(TEXT("actors"), ActorsArray);
	Response->SetNumberField(TEXT("actor_count"), ActorsArray.Num());
	Response->SetBoolField(TEXT("limit_reached"), bLimitReached);
	Response->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());

	return FMcpToolResult::StructuredJson(Response);
}

TSharedPtr<FJsonObject> UQueryWorldTool::SerializeActor(
	AActor* Actor,
	bool bIncludeComponents,
	bool bIncludeTransform,
	bool bIncludeProperties,
	bool bIncludeTags,
	bool bIncludeBounds) const
{
	return QueryWorldToolPrivate::SerializeActor(
		Actor,
		bIncludeComponents,
		bIncludeTransform,
		bIncludeProperties,
		bIncludeTags,
		bIncludeBounds,
		false,
		TEXT(""));
}

TSharedPtr<FJsonObject> UQueryWorldTool::SerializeComponent(
	UActorComponent* Component,
	bool bIncludeProperties) const
{
	return QueryWorldToolPrivate::SerializeComponent(Component, bIncludeProperties, false);
}
