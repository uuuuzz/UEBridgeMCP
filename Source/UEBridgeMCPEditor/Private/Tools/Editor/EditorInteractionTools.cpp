// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Editor/EditorInteractionTools.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UnrealClient.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> VectorToJsonArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	TArray<TSharedPtr<FJsonValue>> RotatorToJsonArray(const FRotator& Rotator)
	{
		return {
			MakeShareable(new FJsonValueNumber(Rotator.Pitch)),
			MakeShareable(new FJsonValueNumber(Rotator.Yaw)),
			MakeShareable(new FJsonValueNumber(Rotator.Roll))
		};
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Array) || !Array || Array->Num() < 3)
		{
			return false;
		}

		OutVector.X = (*Array)[0]->AsNumber();
		OutVector.Y = (*Array)[1]->AsNumber();
		OutVector.Z = (*Array)[2]->AsNumber();
		return true;
	}

	bool ReadRotator(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FRotator& OutRotator)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Array) || !Array || Array->Num() < 3)
		{
			return false;
		}

		OutRotator.Pitch = (*Array)[0]->AsNumber();
		OutRotator.Yaw = (*Array)[1]->AsNumber();
		OutRotator.Roll = (*Array)[2]->AsNumber();
		return true;
	}

	UWorld* ResolveEditorWorld()
	{
		return FMcpAssetModifier::ResolveWorld(TEXT("editor"));
	}

	AActor* ResolveActorByName(UWorld* World, const FString& ActorName)
	{
		if (!World || ActorName.IsEmpty())
		{
			return nullptr;
		}
		return FMcpAssetModifier::FindActorByName(World, ActorName);
	}

	TArray<TSharedPtr<FJsonValue>> BuildSelectionArray(UWorld* World, const FString& SessionId)
	{
		TArray<TSharedPtr<FJsonValue>> SelectedActors;
		if (!GEditor || !World || !GEditor->GetSelectedActors())
		{
			return SelectedActors;
		}

		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			if (Actor && Actor->GetWorld() == World)
			{
				SelectedActors.Add(MakeShareable(new FJsonValueObject(
					McpV2ToolUtils::SerializeActorSummary(Actor, SessionId, true, false))));
			}
		}
		return SelectedActors;
	}

	FEditorViewportClient* GetActiveEditorViewportClient()
	{
		if (!GEditor || !GEditor->GetActiveViewport())
		{
			return nullptr;
		}
		return static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
	}

	bool IsAllowedEditorCommand(const FString& Command)
	{
		const FString Trimmed = Command.TrimStartAndEnd();
		const FString Lower = Trimmed.ToLower();
		return
			Lower.StartsWith(TEXT("stat ")) ||
			Lower.StartsWith(TEXT("viewmode ")) ||
			Lower.StartsWith(TEXT("showflag.")) ||
			Lower.Equals(TEXT("highresshot")) ||
			Lower.StartsWith(TEXT("r.")) ||
			Lower.StartsWith(TEXT("wp."));
	}
}

FString UEditEditorSelectionTool::GetToolDescription() const
{
	return TEXT("Edit the current editor actor selection using clear, select, add, remove, and toggle actions.");
}

TMap<FString, FMcpSchemaProperty> UEditEditorSelectionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("action"), FMcpSchemaProperty::MakeEnum(TEXT("Selection action"), { TEXT("clear"), TEXT("select"), TEXT("add"), TEXT("remove"), TEXT("toggle") }, true));
	Schema.Add(TEXT("actor_names"), FMcpSchemaProperty::MakeArray(TEXT("Actor labels or object names"), TEXT("string")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without changing editor selection")));
	return Schema;
}

FMcpToolResult UEditEditorSelectionTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	if (!GEditor)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_EDITOR_UNAVAILABLE"), TEXT("GEditor is not available"));
	}

	UWorld* World = ResolveEditorWorld();
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_AVAILABLE"), TEXT("No editor world is currently available"));
	}

	const FString Action = GetStringArgOrDefault(Arguments, TEXT("action")).ToLower();
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	const TArray<TSharedPtr<FJsonValue>>* ActorNames = nullptr;
	Arguments->TryGetArrayField(TEXT("actor_names"), ActorNames);

	TArray<AActor*> ResolvedActors;
	TArray<TSharedPtr<FJsonValue>> MissingActors;
	if (Action != TEXT("clear"))
	{
		if (!ActorNames || ActorNames->Num() == 0)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'actor_names' is required unless action is 'clear'"));
		}

		for (const TSharedPtr<FJsonValue>& Value : *ActorNames)
		{
			const FString ActorName = Value.IsValid() ? Value->AsString() : FString();
			AActor* Actor = ResolveActorByName(World, ActorName);
			if (Actor)
			{
				ResolvedActors.Add(Actor);
			}
			else
			{
				MissingActors.Add(MakeShareable(new FJsonValueString(ActorName)));
			}
		}
	}

	if (!bDryRun)
	{
		if (Action == TEXT("clear") || Action == TEXT("select"))
		{
			GEditor->SelectNone(false, true, false);
		}

		if (Action == TEXT("select") || Action == TEXT("add") || Action == TEXT("toggle") || Action == TEXT("remove"))
		{
			for (AActor* Actor : ResolvedActors)
			{
				const bool bCurrentlySelected = Actor->IsSelected();
				bool bShouldSelect = bCurrentlySelected;
				if (Action == TEXT("select") || Action == TEXT("add"))
				{
					bShouldSelect = true;
				}
				else if (Action == TEXT("remove"))
				{
					bShouldSelect = false;
				}
				else if (Action == TEXT("toggle"))
				{
					bShouldSelect = !bCurrentlySelected;
				}
				GEditor->SelectActor(Actor, bShouldSelect, false, true, false);
			}
		}
		GEditor->NoteSelectionChange();
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), MissingActors.Num() == 0);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("action"), Action);
	Response->SetNumberField(TEXT("resolved_count"), ResolvedActors.Num());
	Response->SetArrayField(TEXT("missing_actors"), MissingActors);
	Response->SetArrayField(TEXT("selected_actors"), BuildSelectionArray(World, Context.SessionId));
	return FMcpToolResult::StructuredJson(Response, MissingActors.Num() > 0);
}

FString UEditViewportCameraTool::GetToolDescription() const
{
	return TEXT("Move or query the active level editor viewport camera using location, rotation, and optional FOV.");
}

TMap<FString, FMcpSchemaProperty> UEditViewportCameraTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("action"), FMcpSchemaProperty::MakeEnum(TEXT("Viewport camera action"), { TEXT("query"), TEXT("set") }));
	Schema.Add(TEXT("location"), FMcpSchemaProperty::MakeArray(TEXT("Camera location [x,y,z]"), TEXT("number")));
	Schema.Add(TEXT("rotation"), FMcpSchemaProperty::MakeArray(TEXT("Camera rotation [pitch,yaw,roll]"), TEXT("number")));
	Schema.Add(TEXT("fov"), FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional perspective FOV")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without moving the camera")));
	return Schema;
}

FMcpToolResult UEditViewportCameraTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	FEditorViewportClient* ViewportClient = GetActiveEditorViewportClient();
	if (!ViewportClient)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_VIEWPORT_UNAVAILABLE"), TEXT("No active editor viewport client is available"));
	}

	const FString Action = GetStringArgOrDefault(Arguments, TEXT("action"), TEXT("query")).ToLower();
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	FVector NewLocation;
	FRotator NewRotation;
	const bool bHasLocation = ReadVector(Arguments, TEXT("location"), NewLocation);
	const bool bHasRotation = ReadRotator(Arguments, TEXT("rotation"), NewRotation);

	double RequestedFov = 0.0;
	const bool bHasFov = Arguments->TryGetNumberField(TEXT("fov"), RequestedFov);

	if (Action == TEXT("set") && !bDryRun)
	{
		if (bHasLocation)
		{
			ViewportClient->SetViewLocation(NewLocation);
		}
		if (bHasRotation)
		{
			ViewportClient->SetViewRotation(NewRotation);
		}
		if (bHasFov)
		{
			ViewportClient->ViewFOV = FMath::Clamp(static_cast<float>(RequestedFov), 5.0f, 170.0f);
		}
		ViewportClient->Invalidate();
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("action"), Action);
	Response->SetArrayField(TEXT("location"), VectorToJsonArray(ViewportClient->GetViewLocation()));
	Response->SetArrayField(TEXT("rotation"), RotatorToJsonArray(ViewportClient->GetViewRotation()));
	Response->SetNumberField(TEXT("fov"), ViewportClient->ViewFOV);
	return FMcpToolResult::StructuredJson(Response);
}

FString URunEditorCommandTool::GetToolDescription() const
{
	return TEXT("Run a curated editor console command. By default only safe diagnostic/view commands are allowed.");
}

TMap<FString, FMcpSchemaProperty> URunEditorCommandTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("command"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Editor console command to execute"), true));
	Schema.Add(TEXT("allow_unsafe"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Allow commands outside the built-in safe whitelist. Default: false.")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without running the command")));
	return Schema;
}

FMcpToolResult URunEditorCommandTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString Command = GetStringArgOrDefault(Arguments, TEXT("command")).TrimStartAndEnd();
	const bool bAllowUnsafe = GetBoolArgOrDefault(Arguments, TEXT("allow_unsafe"), false);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	if (Command.IsEmpty())
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'command' is required"));
	}
	if (!bAllowUnsafe && !IsAllowedEditorCommand(Command))
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("command"), Command);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_UNSAFE_EDITOR_COMMAND"), TEXT("Command is outside the safe editor command whitelist"), Details);
	}

	bool bExecuted = false;
	if (!bDryRun)
	{
		UWorld* World = ResolveEditorWorld();
		bExecuted = GEditor ? GEditor->Exec(World, *Command) : false;
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), bDryRun || bExecuted);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("command"), Command);
	Response->SetBoolField(TEXT("executed"), bExecuted);
	Response->SetBoolField(TEXT("allow_unsafe"), bAllowUnsafe);
	return FMcpToolResult::StructuredJson(Response, !bDryRun && !bExecuted);
}
