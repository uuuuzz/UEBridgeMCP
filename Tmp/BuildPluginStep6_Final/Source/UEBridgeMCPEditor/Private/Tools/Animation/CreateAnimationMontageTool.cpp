// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Animation/CreateAnimationMontageTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/AnimMontageFactory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"

FString UCreateAnimationMontageTool::GetToolDescription() const
{
	return TEXT("Create an animation montage from one or more AnimSequence assets using the default slot track.");
}

TMap<FString, FMcpSchemaProperty> UCreateAnimationMontageTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("New montage asset path"), true));
	Schema.Add(TEXT("sequence_paths"), FMcpSchemaProperty::MakeArray(TEXT("Source AnimSequence asset paths"), TEXT("string"), true));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the created montage")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without creating the montage")));
	return Schema;
}

FMcpToolResult UCreateAnimationMontageTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);

	const TArray<TSharedPtr<FJsonValue>>* SequencePathsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("sequence_paths"), SequencePathsArray) || !SequencePathsArray || SequencePathsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'sequence_paths' array is required"));
	}

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), TEXT("Asset already exists"));
	}

	TArray<UAnimSequence*> Sequences;
	TArray<TSharedPtr<FJsonValue>> SequenceResultArray;
	for (int32 Index = 0; Index < SequencePathsArray->Num(); ++Index)
	{
		const FString SequencePath = (*SequencePathsArray)[Index].IsValid() ? (*SequencePathsArray)[Index]->AsString() : FString();
		FString LoadError;
		UAnimSequence* Sequence = FMcpAssetModifier::LoadAssetByPath<UAnimSequence>(SequencePath, LoadError);
		if (!Sequence)
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
		}
		Sequences.Add(Sequence);
		SequenceResultArray.Add(MakeShareable(new FJsonValueString(SequencePath)));
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), GetToolName());
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("asset_path"), AssetPath);
		Response->SetArrayField(TEXT("sequence_paths"), SequenceResultArray);
		Response->SetStringField(TEXT("skeleton_path"), Sequences[0]->GetSkeleton() ? Sequences[0]->GetSkeleton()->GetPathName() : TEXT(""));
		Response->SetBoolField(TEXT("would_create"), true);
		return FMcpToolResult::StructuredJson(Response);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create Animation Montage")));

	const FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	const FString AssetName = FPackageName::GetShortName(AssetPath);

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Sequences[0]->GetSkeleton();
	Factory->SourceAnimation = Sequences[0];

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* CreatedObject = AssetTools.CreateAsset(AssetName, PackagePath, UAnimMontage::StaticClass(), Factory);
	UAnimMontage* Montage = Cast<UAnimMontage>(CreatedObject);
	if (!Montage)
	{
		if (Transaction.IsValid())
		{
			Transaction->Cancel();
		}
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create animation montage"));
	}

	if (Montage->SlotAnimTracks.Num() == 0)
	{
		Montage->SlotAnimTracks.AddDefaulted();
	}

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[0];
	SlotTrack.AnimTrack.AnimSegments.Reset();

	float CurrentStartTime = 0.0f;
	for (UAnimSequence* Sequence : Sequences)
	{
		FAnimSegment Segment;
		Segment.SetAnimReference(Sequence, true);
		Segment.StartPos = CurrentStartTime;
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = Sequence->GetPlayLength();
		Segment.AnimPlayRate = 1.0f;
		Segment.LoopingCount = 1;
		SlotTrack.AnimTrack.AnimSegments.Add(Segment);
		CurrentStartTime += Segment.GetLength();
	}

	UAnimMontageFactory::EnsureStartingSection(Montage);

	FAssetRegistryModule::AssetCreated(Montage);
	FMcpAssetModifier::MarkPackageDirty(Montage);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(Montage, false, SaveError))
		{
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Montage->GetClass()->GetName()));
	Response->SetArrayField(TEXT("sequence_paths"), SequenceResultArray);
	Response->SetNumberField(TEXT("duration"), Montage->GetPlayLength());
	Response->SetStringField(TEXT("skeleton_path"), Montage->GetSkeleton() ? Montage->GetSkeleton()->GetPathName() : TEXT(""));
	return FMcpToolResult::StructuredJson(Response);
}
