// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Widget/EditWidgetAnimationTool.h"
#include "Utils/McpAssetModifier.h"
#include "Animation/WidgetAnimation.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieScene.h"
#include "WidgetBlueprint.h"

namespace
{
static UWidgetAnimation* FindAnimationByName(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName)
{
	if (!WidgetBlueprint)
	{
		return nullptr;
	}

	for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (!Animation)
		{
			continue;
		}

		if (Animation->GetName().Equals(AnimationName, ESearchCase::IgnoreCase) ||
			Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			return Animation;
		}
	}

	return nullptr;
}

static FFrameRate MakeDisplayRate(double DisplayRateFps)
{
	const int32 RoundedFps = FMath::Max(1, FMath::RoundToInt(DisplayRateFps));
	if (FMath::IsNearlyEqual(DisplayRateFps, static_cast<double>(RoundedFps), KINDA_SMALL_NUMBER))
	{
		return FFrameRate(RoundedFps, 1);
	}

	return FFrameRate(FMath::Max(1, FMath::RoundToInt(DisplayRateFps * 1000.0)), 1000);
}

static bool ApplyPlaybackRange(
	UWidgetAnimation* Animation,
	double StartTimeSeconds,
	double EndTimeSeconds,
	TOptional<double> DisplayRateFps,
	FString& OutError)
{
	if (!Animation || !Animation->MovieScene)
	{
		OutError = TEXT("Animation has no MovieScene");
		return false;
	}

	if (EndTimeSeconds <= StartTimeSeconds)
	{
		OutError = TEXT("'end_time' must be greater than 'start_time'");
		return false;
	}

	Animation->Modify();
	Animation->MovieScene->Modify();

	if (DisplayRateFps.IsSet())
	{
		if (DisplayRateFps.GetValue() <= 0.0)
		{
			OutError = TEXT("'display_rate_fps' must be greater than 0");
			return false;
		}

		Animation->MovieScene->SetDisplayRate(MakeDisplayRate(DisplayRateFps.GetValue()));
	}

	const FFrameRate TickResolution = Animation->MovieScene->GetTickResolution();
	const FFrameTime InFrame = StartTimeSeconds * TickResolution;
	const FFrameTime OutFrame = EndTimeSeconds * TickResolution;
	Animation->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(InFrame.FrameNumber, OutFrame.FrameNumber + 1));
	Animation->MovieScene->GetEditorData().WorkStart = StartTimeSeconds;
	Animation->MovieScene->GetEditorData().WorkEnd = EndTimeSeconds;
	return true;
}
}

FString UEditWidgetAnimationTool::GetToolDescription() const
{
	return TEXT("Edit Widget Blueprint animation assets in batch. Supports creating empty animations, "
		"renaming, removing, and setting playback ranges.");
}

TMap<FString, FMcpSchemaProperty> UEditWidgetAnimationTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Widget Blueprint asset path"),
		true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Widget animation edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Animation edit action"),
		{ TEXT("create_animation"), TEXT("rename_animation"), TEXT("remove_animation"), TEXT("set_playback_range") },
		true)));
	OperationSchema->Properties.Add(TEXT("animation_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("Target animation name or display label"))));
	OperationSchema->Properties.Add(TEXT("new_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("string"),
		TEXT("New animation name for rename_animation"))));
	OperationSchema->Properties.Add(TEXT("start_time"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("number"),
		TEXT("Playback range start time in seconds"))));
	OperationSchema->Properties.Add(TEXT("end_time"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("number"),
		TEXT("Playback range end time in seconds"))));
	OperationSchema->Properties.Add(TEXT("display_rate_fps"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(
		TEXT("number"),
		TEXT("Optional display frame rate in frames per second"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Array of animation edit operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("compile"), FMcpSchemaProperty::MakeEnum(
		TEXT("Compile policy: 'never', 'if_needed', or 'always'"),
		{ TEXT("never"), TEXT("if_needed"), TEXT("always") }));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after successful edits")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel batch on first failure")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional transaction label")));

	return Schema;
}

FMcpToolResult UEditWidgetAnimationTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'asset_path' is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), OperationsArray) || !OperationsArray || OperationsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const FString CompilePolicy = GetStringArgOrDefault(Arguments, TEXT("compile"), TEXT("if_needed"));
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Widget Animation"));

	FString LoadError;
	UWidgetBlueprint* WidgetBlueprint = FMcpAssetModifier::LoadAssetByPath<UWidgetBlueprint>(AssetPath, LoadError);
	if (!WidgetBlueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
		WidgetBlueprint->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < OperationsArray->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonValue>& OperationValue = (*OperationsArray)[OperationIndex];
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!OperationValue.IsValid() || !OperationValue->TryGetObject(OperationObject) || !(*OperationObject).IsValid())
		{
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Operation at index %d is not a valid object"), OperationIndex));
			}
			continue;
		}

		TSharedPtr<FJsonObject> OperationResult = MakeShareable(new FJsonObject);
		OperationResult->SetNumberField(TEXT("index"), OperationIndex);

		FString ActionName;
		if (!(*OperationObject)->TryGetStringField(TEXT("action"), ActionName))
		{
			OperationResult->SetBoolField(TEXT("success"), false);
			OperationResult->SetStringField(TEXT("error"), TEXT("Missing 'action' field"));
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
			ResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
			bAnyFailed = true;
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"),
					FString::Printf(TEXT("Operation at index %d is missing 'action'"), OperationIndex));
			}
			continue;
		}

		OperationResult->SetStringField(TEXT("action"), ActionName);
		FString OperationError;
		bool bOperationSuccess = false;
		bool bOperationChanged = false;

		if (ActionName == TEXT("create_animation"))
		{
			FString AnimationName;
			if (!(*OperationObject)->TryGetStringField(TEXT("animation_name"), AnimationName))
			{
				OperationError = TEXT("'animation_name' is required for create_animation");
			}
			else if (FindAnimationByName(WidgetBlueprint, AnimationName))
			{
				OperationError = FString::Printf(TEXT("Animation '%s' already exists"), *AnimationName);
			}
			else
			{
				double StartTime = 0.0;
				double EndTime = 1.0;
				double DisplayRateFps = 20.0;
				(*OperationObject)->TryGetNumberField(TEXT("start_time"), StartTime);
				(*OperationObject)->TryGetNumberField(TEXT("end_time"), EndTime);
				(*OperationObject)->TryGetNumberField(TEXT("display_rate_fps"), DisplayRateFps);

				OperationResult->SetStringField(TEXT("animation_name"), AnimationName);
				if (!bDryRun)
				{
					UWidgetAnimation* NewAnimation = NewObject<UWidgetAnimation>(
						WidgetBlueprint,
						UWidgetAnimation::StaticClass(),
						FName(*AnimationName),
						RF_Transactional);
					if (!NewAnimation)
					{
						OperationError = FString::Printf(TEXT("Failed to create animation '%s'"), *AnimationName);
					}
					else
					{
						NewAnimation->Modify();
						NewAnimation->SetDisplayLabel(AnimationName);
						NewAnimation->MovieScene = NewObject<UMovieScene>(NewAnimation, FName(*AnimationName), RF_Transactional);
						if (!NewAnimation->MovieScene)
						{
							OperationError = TEXT("Failed to create MovieScene for animation");
						}
						else if (!ApplyPlaybackRange(NewAnimation, StartTime, EndTime, DisplayRateFps, OperationError))
						{
							// Error already set.
						}
						else
						{
							WidgetBlueprint->Animations.Add(NewAnimation);
							WidgetBlueprint->OnVariableAdded(NewAnimation->GetFName());
							FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
							bOperationSuccess = true;
							bOperationChanged = true;
						}
					}
				}
				else
				{
					if (EndTime <= StartTime)
					{
						OperationError = TEXT("'end_time' must be greater than 'start_time'");
					}
					else if (DisplayRateFps <= 0.0)
					{
						OperationError = TEXT("'display_rate_fps' must be greater than 0");
					}
					else
					{
						bOperationSuccess = true;
						bOperationChanged = true;
					}
				}
			}
		}
		else if (ActionName == TEXT("rename_animation"))
		{
			FString AnimationName;
			FString NewName;
			if (!(*OperationObject)->TryGetStringField(TEXT("animation_name"), AnimationName) ||
				!(*OperationObject)->TryGetStringField(TEXT("new_name"), NewName))
			{
				OperationError = TEXT("'animation_name' and 'new_name' are required for rename_animation");
			}
			else
			{
				UWidgetAnimation* Animation = FindAnimationByName(WidgetBlueprint, AnimationName);
				if (!Animation)
				{
					OperationError = FString::Printf(TEXT("Animation '%s' not found"), *AnimationName);
				}
				else if (UWidgetAnimation* ExistingAnimation = FindAnimationByName(WidgetBlueprint, NewName))
				{
					if (ExistingAnimation != Animation)
					{
						OperationError = FString::Printf(TEXT("Animation '%s' already exists"), *NewName);
					}
				}

				if (OperationError.IsEmpty())
				{
					OperationResult->SetStringField(TEXT("animation_name"), AnimationName);
					OperationResult->SetStringField(TEXT("new_name"), NewName);
					if (!bDryRun)
					{
						const FName OldName = Animation->GetFName();
						const FName NewFName = FName(*NewName);
						Animation->Modify();
						if (Animation->MovieScene)
						{
							Animation->MovieScene->Modify();
						}
						Animation->SetDisplayLabel(NewName);
						Animation->Rename(*NewName, nullptr, REN_DontCreateRedirectors);
						if (Animation->MovieScene)
						{
							Animation->MovieScene->Rename(*NewName, nullptr, REN_DontCreateRedirectors);
						}
						WidgetBlueprint->OnVariableRenamed(OldName, NewFName);
						FBlueprintEditorUtils::ReplaceVariableReferences(WidgetBlueprint, OldName, NewFName);
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
					}

					bOperationSuccess = true;
					bOperationChanged = AnimationName != NewName;
				}
			}
		}
		else if (ActionName == TEXT("remove_animation"))
		{
			FString AnimationName;
			if (!(*OperationObject)->TryGetStringField(TEXT("animation_name"), AnimationName))
			{
				OperationError = TEXT("'animation_name' is required for remove_animation");
			}
			else
			{
				UWidgetAnimation* Animation = FindAnimationByName(WidgetBlueprint, AnimationName);
				if (!Animation)
				{
					OperationError = FString::Printf(TEXT("Animation '%s' not found"), *AnimationName);
				}
				else
				{
					OperationResult->SetStringField(TEXT("animation_name"), AnimationName);
					if (!bDryRun)
					{
						const FName RemovedAnimationName = Animation->GetFName();
						Animation->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
						WidgetBlueprint->Animations.Remove(Animation);
						WidgetBlueprint->OnVariableRemoved(RemovedAnimationName);
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
					}

					bOperationSuccess = true;
					bOperationChanged = true;
				}
			}
		}
		else if (ActionName == TEXT("set_playback_range"))
		{
			FString AnimationName;
			double StartTime = 0.0;
			double EndTime = 0.0;
			if (!(*OperationObject)->TryGetStringField(TEXT("animation_name"), AnimationName) ||
				!(*OperationObject)->TryGetNumberField(TEXT("start_time"), StartTime) ||
				!(*OperationObject)->TryGetNumberField(TEXT("end_time"), EndTime))
			{
				OperationError = TEXT("'animation_name', 'start_time', and 'end_time' are required for set_playback_range");
			}
			else
			{
				UWidgetAnimation* Animation = FindAnimationByName(WidgetBlueprint, AnimationName);
				if (!Animation)
				{
					OperationError = FString::Printf(TEXT("Animation '%s' not found"), *AnimationName);
				}
				else
				{
					TOptional<double> DisplayRateFps;
					double ParsedDisplayRateFps = 0.0;
					if ((*OperationObject)->TryGetNumberField(TEXT("display_rate_fps"), ParsedDisplayRateFps))
					{
						DisplayRateFps = ParsedDisplayRateFps;
					}

					OperationResult->SetStringField(TEXT("animation_name"), AnimationName);
					OperationResult->SetNumberField(TEXT("start_time"), StartTime);
					OperationResult->SetNumberField(TEXT("end_time"), EndTime);

					if (!bDryRun)
					{
						bOperationSuccess = ApplyPlaybackRange(Animation, StartTime, EndTime, DisplayRateFps, OperationError);
						if (bOperationSuccess)
						{
							FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
						}
					}
					else
					{
						if (EndTime <= StartTime)
						{
							OperationError = TEXT("'end_time' must be greater than 'start_time'");
						}
						else if (DisplayRateFps.IsSet() && DisplayRateFps.GetValue() <= 0.0)
						{
							OperationError = TEXT("'display_rate_fps' must be greater than 0");
						}
						else
						{
							bOperationSuccess = true;
						}
					}

					bOperationChanged = bOperationSuccess;
				}
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action: '%s'"), *ActionName);
		}

		OperationResult->SetBoolField(TEXT("success"), bOperationSuccess);
		OperationResult->SetBoolField(TEXT("changed"), bOperationChanged);
		if (!OperationError.IsEmpty())
		{
			OperationResult->SetStringField(TEXT("error"), OperationError);
		}

		if (!bOperationSuccess)
		{
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
			if (!bDryRun && bRollbackOnError)
			{
				Transaction.Reset();
				TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
				Details->SetStringField(TEXT("asset_path"), AssetPath);
				Details->SetNumberField(TEXT("failed_operation_index"), OperationIndex);
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ACTION"), OperationError, Details);
			}
		}
		else
		{
			bAnyChanged |= bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(OperationResult)));
	}

	if (!bDryRun && bAnyChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(WidgetBlueprint);
	}

	bool bCompileAttempted = false;
	bool bCompileSuccess = true;
	FString CompileError;
	if (!bDryRun && bAnyChanged)
	{
		const bool bShouldCompile = (CompilePolicy == TEXT("always")) ||
			(CompilePolicy == TEXT("if_needed"));
		if (bShouldCompile)
		{
			bCompileAttempted = true;
			FMcpAssetModifier::RefreshBlueprintNodes(WidgetBlueprint);
			bCompileSuccess = FMcpAssetModifier::CompileBlueprint(WidgetBlueprint, CompileError);
			if (!bCompileSuccess)
			{
				TSharedPtr<FJsonObject> DiagnosticObject = MakeShareable(new FJsonObject);
				DiagnosticObject->SetStringField(TEXT("severity"), TEXT("error"));
				DiagnosticObject->SetStringField(TEXT("code"), TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"));
				DiagnosticObject->SetStringField(TEXT("message"), CompileError);
				DiagnosticObject->SetStringField(TEXT("asset_path"), AssetPath);
				DiagnosticsArray.Add(MakeShareable(new FJsonValueObject(DiagnosticObject)));
				bAnyFailed = true;
			}
		}
	}

	if (!bDryRun && bSave && !bAnyFailed)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(WidgetBlueprint, false, SaveError) && !SaveError.IsEmpty())
		{
			WarningsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Save failed: %s"), *SaveError))));
		}
	}

	TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
	CompileObject->SetBoolField(TEXT("attempted"), bCompileAttempted);
	CompileObject->SetBoolField(TEXT("success"), bCompileSuccess);
	if (!CompileError.IsEmpty())
	{
		CompileObject->SetStringField(TEXT("error"), CompileError);
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);

	TArray<TSharedPtr<FJsonValue>> ModifiedAssets;
	ModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssets);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetObjectField(TEXT("compile"), CompileObject);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);

	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
