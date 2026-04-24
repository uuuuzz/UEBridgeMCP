// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Write/AddComponentTool.h"
#include "Utils/McpAssetModifier.h"
#include "UEBridgeMCPEditor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "ScopedTransaction.h"

FString UAddComponentTool::GetToolDescription() const
{
	return TEXT("Add a component to an existing actor in the level.");
}

TMap<FString, FMcpSchemaProperty> UAddComponentTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty ActorName;
	ActorName.Type = TEXT("string");
	ActorName.Description = TEXT("Actor name or label to add component to");
	ActorName.bRequired = true;
	Schema.Add(TEXT("actor_name"), ActorName);

	FMcpSchemaProperty ComponentClass;
	ComponentClass.Type = TEXT("string");
	ComponentClass.Description = TEXT("Component class (e.g., 'PointLightComponent', 'StaticMeshComponent', 'BoxComponent')");
	ComponentClass.bRequired = true;
	Schema.Add(TEXT("component_class"), ComponentClass);

	FMcpSchemaProperty ComponentName;
	ComponentName.Type = TEXT("string");
	ComponentName.Description = TEXT("Name for the new component");
	ComponentName.bRequired = false;
	Schema.Add(TEXT("component_name"), ComponentName);

	FMcpSchemaProperty AttachTo;
	AttachTo.Type = TEXT("string");
	AttachTo.Description = TEXT("Parent component name to attach to");
	AttachTo.bRequired = false;
	Schema.Add(TEXT("attach_to"), AttachTo);

	return Schema;
}

TArray<FString> UAddComponentTool::GetRequiredParams() const
{
	return { TEXT("actor_name"), TEXT("component_class") };
}

FMcpToolResult UAddComponentTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	FString ComponentClass = GetStringArgOrDefault(Arguments, TEXT("component_class"));
	FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	FString AttachTo = GetStringArgOrDefault(Arguments, TEXT("attach_to"));

	if (ActorName.IsEmpty() || ComponentClass.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("actor_name and component_class are required"));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("add-component: %s to %s"), *ComponentClass, *ActorName);

	// Get the editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FMcpToolResult::Error(TEXT("No world available. Open a level first."));
	}

	// Find the actor
	AActor* FoundActor = nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor)
		{
			if (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase) ||
				Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				FoundActor = Actor;
				break;
			}
		}
	}

	if (!FoundActor)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	// Find component class
	UClass* CompClass = nullptr;

	if (ComponentClass.Equals(TEXT("PointLightComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = UPointLightComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("SpotLightComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = USpotLightComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("StaticMeshComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = UStaticMeshComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("BoxComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = UBoxComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("SphereComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = USphereComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("CapsuleComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = UCapsuleComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("AudioComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = UAudioComponent::StaticClass();
	}
	else if (ComponentClass.Equals(TEXT("SceneComponent"), ESearchCase::IgnoreCase))
	{
		CompClass = USceneComponent::StaticClass();
	}
	else
	{
		CompClass = FindFirstObject<UClass>(*ComponentClass, EFindFirstObjectOptions::ExactClass);
		if (!CompClass)
		{
			CompClass = FindFirstObject<UClass>(*(TEXT("U") + ComponentClass), EFindFirstObjectOptions::ExactClass);
		}
	}

	if (!CompClass)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
	}

	// Begin transaction
	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(
		FText::Format(NSLOCTEXT("MCP", "AddComponent", "Add {0} to {1}"),
			FText::FromString(ComponentClass), FText::FromString(ActorName)));

	FoundActor->Modify();

	// Create the component
	FName CompFName = ComponentName.IsEmpty() ? FName(*ComponentClass) : FName(*ComponentName);
	UActorComponent* NewComponent = NewObject<UActorComponent>(FoundActor, CompClass, CompFName, RF_Transactional);

	if (!NewComponent)
	{
		return FMcpToolResult::Error(TEXT("Failed to create component"));
	}

	// Attach if it's a scene component
	if (USceneComponent* SceneComp = Cast<USceneComponent>(NewComponent))
	{
		USceneComponent* ParentComp = FoundActor->GetRootComponent();

		if (!AttachTo.IsEmpty())
		{
			// Find the parent component
			TArray<USceneComponent*> SceneComponents;
			FoundActor->GetComponents(SceneComponents);

			for (USceneComponent* Comp : SceneComponents)
			{
				if (Comp && Comp->GetName().Equals(AttachTo, ESearchCase::IgnoreCase))
				{
					ParentComp = Comp;
					break;
				}
			}
		}

		if (ParentComp)
		{
			SceneComp->SetupAttachment(ParentComp);
		}
		else
		{
			FoundActor->SetRootComponent(SceneComp);
		}
	}

	// Register the component
	NewComponent->RegisterComponent();
	FoundActor->AddInstanceComponent(NewComponent);

	// Mark level as dirty
	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor_name"), FoundActor->GetName());
	Result->SetStringField(TEXT("component_name"), NewComponent->GetName());
	Result->SetStringField(TEXT("component_class"), CompClass->GetName());
	Result->SetBoolField(TEXT("needs_save"), true);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("add-component: Added %s to %s"), *NewComponent->GetName(), *FoundActor->GetName());

	return FMcpToolResult::Json(Result);
}
