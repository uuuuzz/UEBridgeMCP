// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Niagara/ApplyNiagaraSystemToActorTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Tools/Niagara/NiagaraToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"

namespace
{
	UNiagaraComponent* FindNiagaraComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		TArray<UNiagaraComponent*> Components;
		Actor->GetComponents<UNiagaraComponent>(Components);

		if (!ComponentName.IsEmpty())
		{
			for (UNiagaraComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					return Component;
				}
			}
			return nullptr;
		}

		return Components.Num() > 0 ? Components[0] : nullptr;
	}

	UNiagaraComponent* CreateNiagaraComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}

		const FName NewComponentName(*(!ComponentName.IsEmpty() ? ComponentName : TEXT("NiagaraMCPComponent")));
		UNiagaraComponent* Component = NewObject<UNiagaraComponent>(Actor, UNiagaraComponent::StaticClass(), NewComponentName, RF_Transactional);
		if (!Component)
		{
			return nullptr;
		}

		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Component->SetupAttachment(Root);
		}
		else
		{
			Actor->SetRootComponent(Component);
		}

		Actor->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}
}

FString UApplyNiagaraSystemToActorTool::GetToolDescription() const
{
	return TEXT("Apply a Niagara system asset to an editor-world actor by creating or updating a NiagaraComponent, with optional user parameter overrides.");
}

TMap<FString, FMcpSchemaProperty> UApplyNiagaraSystemToActorTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("system_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Niagara system asset path"), true));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("Target world"), { TEXT("editor"), TEXT("pie"), TEXT("auto") }));
	Schema.Add(TEXT("actor_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name")));
	Schema.Add(TEXT("actor_handle"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle")));
	Schema.Add(TEXT("component_name"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing or new NiagaraComponent name")));
	Schema.Add(TEXT("create_if_missing"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Create NiagaraComponent when no matching component exists")));
	Schema.Add(TEXT("auto_activate"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Set component auto-activation")));
	Schema.Add(TEXT("activate_now"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Activate the component immediately after applying")));

	TSharedPtr<FMcpSchemaProperty> OverrideSchema = MakeShared<FMcpSchemaProperty>();
	OverrideSchema->Type = TEXT("object");
	OverrideSchema->Description = TEXT("Niagara component user parameter override");
	OverrideSchema->NestedRequired = { TEXT("name"), TEXT("type"), TEXT("value") };
	OverrideSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Parameter name, with or without User. prefix"), true)));
	OverrideSchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Parameter type"),
		{ TEXT("bool"), TEXT("int32"), TEXT("float"), TEXT("vector2"), TEXT("vector3"), TEXT("position"), TEXT("vector4"), TEXT("color") },
		true)));
	OverrideSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Override value"), true)));

	FMcpSchemaProperty OverridesSchema;
	OverridesSchema.Type = TEXT("array");
	OverridesSchema.Description = TEXT("Optional parameter overrides to apply on the component");
	OverridesSchema.Items = OverrideSchema;
	Schema.Add(TEXT("overrides"), OverridesSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the edited map when possible")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on failure")));
	return Schema;
}

FMcpToolResult UApplyNiagaraSystemToActorTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString SystemPath = GetStringArgOrDefault(Arguments, TEXT("system_path"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const FString ComponentName = GetStringArgOrDefault(Arguments, TEXT("component_name"));
	const bool bCreateIfMissing = GetBoolArgOrDefault(Arguments, TEXT("create_if_missing"), true);
	const bool bAutoActivate = GetBoolArgOrDefault(Arguments, TEXT("auto_activate"), true);
	const bool bActivateNow = GetBoolArgOrDefault(Arguments, TEXT("activate_now"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	FString LoadError;
	UNiagaraSystem* System = FMcpAssetModifier::LoadAssetByPath<UNiagaraSystem>(SystemPath, LoadError);
	if (!System)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UWorld* World = nullptr;
	FString ErrorCode;
	FString ErrorMessage;
	TSharedPtr<FJsonObject> ErrorDetails;
	AActor* Actor = LevelActorToolUtils::ResolveActorReference(
		Arguments,
		WorldType,
		TEXT("actor_name"),
		TEXT("actor_handle"),
		Context,
		World,
		ErrorCode,
		ErrorMessage,
		ErrorDetails,
		true);
	if (!Actor)
	{
		return FMcpToolResult::StructuredError(ErrorCode, ErrorMessage, ErrorDetails);
	}

	const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;
	Arguments->TryGetArrayField(TEXT("overrides"), Overrides);

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Apply Niagara System To Actor")));
		Actor->Modify();
	}

	UNiagaraComponent* Component = FindNiagaraComponent(Actor, ComponentName);
	const bool bWouldCreateComponent = !Component && bCreateIfMissing;
	if (!Component && !bDryRun && bCreateIfMissing)
	{
		Component = CreateNiagaraComponent(Actor, ComponentName);
	}

	if (!Component && !bWouldCreateComponent)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_COMPONENT_NOT_FOUND"), TEXT("Niagara component not found and create_if_missing is false"));
	}

	if (!Component && bDryRun && bWouldCreateComponent)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("tool"), GetToolName());
		Result->SetBoolField(TEXT("dry_run"), true);
		Result->SetBoolField(TEXT("would_create_component"), true);
		Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
		Result->SetStringField(TEXT("component_name"), ComponentName.IsEmpty() ? TEXT("NiagaraMCPComponent") : ComponentName);
		Result->SetStringField(TEXT("system_path"), SystemPath);
		Result->SetNumberField(TEXT("override_count"), Overrides ? Overrides->Num() : 0);
		return FMcpToolResult::StructuredSuccess(Result, TEXT("Niagara apply dry run complete"));
	}

	TArray<TSharedPtr<FJsonValue>> OverrideResults;
	bool bOverridesFailed = false;
	if (Overrides)
	{
		for (int32 Index = 0; Index < Overrides->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* OverrideObject = nullptr;
			if (!(*Overrides)[Index].IsValid() || !(*Overrides)[Index]->TryGetObject(OverrideObject) || !OverrideObject || !(*OverrideObject).IsValid())
			{
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("overrides[%d] must be an object"), Index));
			}

			FString Name;
			FString Type;
			const TSharedPtr<FJsonValue> Value = (*OverrideObject)->TryGetField(TEXT("value"));
			(*OverrideObject)->TryGetStringField(TEXT("name"), Name);
			(*OverrideObject)->TryGetStringField(TEXT("type"), Type);

			TSharedPtr<FJsonObject> OverrideResult = MakeShareable(new FJsonObject);
			OverrideResult->SetNumberField(TEXT("index"), Index);
			OverrideResult->SetStringField(TEXT("name"), Name);
			OverrideResult->SetStringField(TEXT("type"), Type);

			FString OverrideError;
			const bool bOverrideSuccess = !bDryRun && Value.IsValid()
				? NiagaraToolUtils::ApplyComponentOverride(Component, Name, Type, Value, OverrideError)
				: (!Name.IsEmpty() && !Type.IsEmpty() && Value.IsValid());

			OverrideResult->SetBoolField(TEXT("success"), bOverrideSuccess);
			if (!bOverrideSuccess)
			{
				bOverridesFailed = true;
				OverrideResult->SetStringField(TEXT("error"), OverrideError.IsEmpty() ? TEXT("Invalid override") : OverrideError);
			}
			OverrideResults.Add(MakeShareable(new FJsonValueObject(OverrideResult)));
		}
	}

	if (bOverridesFailed && bRollbackOnError)
	{
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
			Transaction.Reset();
		}
		TSharedPtr<FJsonObject> Partial = MakeShareable(new FJsonObject);
		Partial->SetArrayField(TEXT("overrides"), OverrideResults);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), TEXT("One or more Niagara overrides failed"), nullptr, Partial);
	}

	TArray<TSharedPtr<FJsonValue>> Warnings;
	bool bActivationApplied = false;
	bool bActivationDeferred = false;
	if (!bDryRun)
	{
		Component->Modify();
		Component->SetAsset(System, true);
		Component->SetAutoActivate(bAutoActivate);
		if (bActivateNow)
		{
			if (World && World->WorldType == EWorldType::Editor)
			{
				bActivationDeferred = true;
				Warnings.Add(MakeShareable(new FJsonValueString(TEXT("activate_now was requested but skipped in the editor world; the NiagaraComponent asset and overrides were applied without starting simulation."))));
			}
			else
			{
				Component->Activate(true);
				bActivationApplied = true;
			}
		}
		else
		{
			if (World && World->WorldType != EWorldType::Editor)
			{
				Component->Deactivate();
			}
		}
		Component->PostEditChange();
		Actor->PostEditChange();
	}

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	if (!bDryRun && World)
	{
		LevelActorToolUtils::AppendWorldModifiedAsset(World, ModifiedAssets);
		FString SaveErrorCode;
		FString SaveErrorMessage;
		if (!LevelActorToolUtils::SaveWorldIfNeeded(World, bSave, Warnings, ModifiedAssets, SaveErrorCode, SaveErrorMessage) && bRollbackOnError)
		{
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();
			}
			return FMcpToolResult::StructuredError(SaveErrorCode, SaveErrorMessage);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetBoolField(TEXT("success"), !bOverridesFailed);
	Result->SetBoolField(TEXT("dry_run"), bDryRun);
	Result->SetStringField(TEXT("system_path"), SystemPath);
	Result->SetStringField(TEXT("actor_name"), Actor->GetActorNameOrLabel());
	Result->SetStringField(TEXT("component_name"), Component ? Component->GetName() : (ComponentName.IsEmpty() ? TEXT("NiagaraMCPComponent") : ComponentName));
	Result->SetBoolField(TEXT("component_created"), bWouldCreateComponent && !bDryRun);
	Result->SetBoolField(TEXT("auto_activate"), bAutoActivate);
	Result->SetBoolField(TEXT("activate_now"), bActivateNow);
	Result->SetBoolField(TEXT("activation_applied"), bActivationApplied);
	Result->SetBoolField(TEXT("activation_deferred"), bActivationDeferred);
	Result->SetArrayField(TEXT("overrides"), OverrideResults);
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	return FMcpToolResult::StructuredJson(Result, bOverridesFailed);
}
