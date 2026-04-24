// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Sequencer/QueryLevelSequenceSummaryTool.h"

#include "Utils/McpAssetModifier.h"

#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"

namespace
{
	TSharedPtr<FJsonObject> SerializeFrameRate(const FFrameRate& FrameRate)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("numerator"), FrameRate.Numerator);
		Object->SetNumberField(TEXT("denominator"), FrameRate.Denominator);
		Object->SetNumberField(TEXT("decimal"), FrameRate.AsDecimal());
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeFrameRange(const TRange<FFrameNumber>& Range)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetBoolField(TEXT("has_start"), Range.HasLowerBound());
		Object->SetBoolField(TEXT("has_end"), Range.HasUpperBound());
		if (Range.HasLowerBound())
		{
			Object->SetNumberField(TEXT("start_frame"), Range.GetLowerBoundValue().Value);
			Object->SetStringField(TEXT("start_bound"), Range.GetLowerBound().IsInclusive() ? TEXT("inclusive") : TEXT("exclusive"));
		}
		if (Range.HasUpperBound())
		{
			Object->SetNumberField(TEXT("end_frame"), Range.GetUpperBoundValue().Value);
			Object->SetStringField(TEXT("end_bound"), Range.GetUpperBound().IsInclusive() ? TEXT("inclusive") : TEXT("exclusive"));
		}
		if (Range.HasLowerBound() && Range.HasUpperBound())
		{
			Object->SetNumberField(TEXT("duration_frames"), Range.GetUpperBoundValue().Value - Range.GetLowerBoundValue().Value);
		}
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeSection(const UMovieSceneSection* Section)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Section)
		{
			return Object;
		}

		Object->SetStringField(TEXT("class"), Section->GetClass()->GetName());
		Object->SetObjectField(TEXT("range"), SerializeFrameRange(Section->GetRange()));
		Object->SetBoolField(TEXT("active"), Section->IsActive());
		Object->SetBoolField(TEXT("locked"), Section->IsLocked());
		return Object;
	}

	TSharedPtr<FJsonObject> SerializeTrack(const UMovieSceneTrack* Track, const int32 MaxSections)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Track)
		{
			return Object;
		}

		const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
		Object->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		Object->SetStringField(TEXT("display_name"), Track->GetDisplayName().ToString());
		Object->SetNumberField(TEXT("section_count"), Sections.Num());

		TArray<TSharedPtr<FJsonValue>> SectionArray;
		const int32 SectionLimit = MaxSections <= 0 ? Sections.Num() : FMath::Min(MaxSections, Sections.Num());
		for (int32 Index = 0; Index < SectionLimit; ++Index)
		{
			SectionArray.Add(MakeShareable(new FJsonValueObject(SerializeSection(Sections[Index]))));
		}
		Object->SetArrayField(TEXT("sections"), SectionArray);
		Object->SetBoolField(TEXT("sections_truncated"), SectionLimit < Sections.Num());
		return Object;
	}

	FString ResolveBindingName(UMovieScene* MovieScene, const FGuid& Guid)
	{
		if (!MovieScene)
		{
			return FString();
		}

		if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid))
		{
			return Possessable->GetName();
		}
		if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid))
		{
			return Spawnable->GetName();
		}
		return FString();
	}

	FString ResolveBindingType(UMovieScene* MovieScene, const FGuid& Guid)
	{
		if (!MovieScene)
		{
			return TEXT("unknown");
		}
		if (MovieScene->FindPossessable(Guid))
		{
			return TEXT("possessable");
		}
		if (MovieScene->FindSpawnable(Guid))
		{
			return TEXT("spawnable");
		}
		return TEXT("unknown");
	}

	UMovieSceneTrack* ResolveCameraCutTrack(UMovieScene* MovieScene)
	{
		if (!MovieScene)
		{
			return nullptr;
		}

		if (UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack())
		{
			return CameraCutTrack;
		}

		// Older tool builds accidentally created camera cuts as a normal master track.
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			if (Track && Track->IsA<UMovieSceneCameraCutTrack>())
			{
				return Track;
			}
		}
		return nullptr;
	}
}

FString UQueryLevelSequenceSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize a Level Sequence asset, including playback range, frame rates, object bindings, tracks, sections, and camera cut counts.");
}

TMap<FString, FMcpSchemaProperty> UQueryLevelSequenceSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("LevelSequence asset path"), true));
	Schema.Add(TEXT("include_tracks"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include per-track and per-section details. Default: true")));
	Schema.Add(TEXT("max_bindings"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum object bindings to include. Default: 64")));
	Schema.Add(TEXT("max_tracks"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum tracks per group or binding to include. Default: 64")));
	Schema.Add(TEXT("max_sections"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum sections per track to include. Default: 32")));
	return Schema;
}

FMcpToolResult UQueryLevelSequenceSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeTracks = GetBoolArgOrDefault(Arguments, TEXT("include_tracks"), true);
	const int32 MaxBindings = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_bindings"), 64));
	const int32 MaxTracks = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_tracks"), 64));
	const int32 MaxSections = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("max_sections"), 32));

	FString LoadError;
	ULevelSequence* LevelSequence = FMcpAssetModifier::LoadAssetByPath<ULevelSequence>(AssetPath, LoadError);
	if (!LevelSequence)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (!MovieScene)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_TYPE"), TEXT("LevelSequence has no MovieScene"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("tool"), GetToolName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_type"), TEXT("LevelSequence"));
	Result->SetStringField(TEXT("movie_scene_name"), MovieScene->GetName());
	Result->SetObjectField(TEXT("playback_range"), SerializeFrameRange(MovieScene->GetPlaybackRange()));
	Result->SetObjectField(TEXT("display_rate"), SerializeFrameRate(MovieScene->GetDisplayRate()));
	Result->SetObjectField(TEXT("tick_resolution"), SerializeFrameRate(MovieScene->GetTickResolution()));

	const UMovieScene* ConstMovieScene = MovieScene;
	const TArray<FMovieSceneBinding>& Bindings = ConstMovieScene->GetBindings();
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetTracks();
	UMovieSceneTrack* CameraCutTrack = ResolveCameraCutTrack(MovieScene);
	const bool bCameraCutIsMasterTrack = CameraCutTrack && MasterTracks.Contains(CameraCutTrack);

	int32 BindingTrackCount = 0;
	int32 SectionCount = 0;
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		BindingTrackCount += Binding.GetTracks().Num();
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (Track)
			{
				SectionCount += Track->GetAllSections().Num();
			}
		}
	}
	for (UMovieSceneTrack* Track : MasterTracks)
	{
		if (Track)
		{
			SectionCount += Track->GetAllSections().Num();
		}
	}
	if (CameraCutTrack && !bCameraCutIsMasterTrack)
	{
		SectionCount += CameraCutTrack->GetAllSections().Num();
	}

	Result->SetNumberField(TEXT("binding_count"), Bindings.Num());
	Result->SetNumberField(TEXT("spawnable_count"), MovieScene->GetSpawnableCount());
	Result->SetNumberField(TEXT("possessable_count"), MovieScene->GetPossessableCount());
	Result->SetNumberField(TEXT("master_track_count"), MasterTracks.Num());
	Result->SetNumberField(TEXT("binding_track_count"), BindingTrackCount);
	Result->SetNumberField(TEXT("track_count"), MasterTracks.Num() + BindingTrackCount + (CameraCutTrack && !bCameraCutIsMasterTrack ? 1 : 0));
	Result->SetNumberField(TEXT("section_count"), SectionCount);
	Result->SetBoolField(TEXT("has_camera_cut_track"), CameraCutTrack != nullptr);
	Result->SetNumberField(TEXT("camera_cut_count"), CameraCutTrack ? CameraCutTrack->GetAllSections().Num() : 0);

	if (bIncludeTracks)
	{
		TArray<TSharedPtr<FJsonValue>> MasterTrackArray;
		const int32 MasterTrackLimit = MaxTracks <= 0 ? MasterTracks.Num() : FMath::Min(MaxTracks, MasterTracks.Num());
		for (int32 Index = 0; Index < MasterTrackLimit; ++Index)
		{
			MasterTrackArray.Add(MakeShareable(new FJsonValueObject(SerializeTrack(MasterTracks[Index], MaxSections))));
		}
		Result->SetArrayField(TEXT("master_tracks"), MasterTrackArray);
		Result->SetBoolField(TEXT("master_tracks_truncated"), MasterTrackLimit < MasterTracks.Num());
		if (CameraCutTrack)
		{
			Result->SetObjectField(TEXT("camera_cut_track"), SerializeTrack(CameraCutTrack, MaxSections));
			Result->SetBoolField(TEXT("camera_cut_track_is_master_track"), bCameraCutIsMasterTrack);
		}

		TArray<TSharedPtr<FJsonValue>> BindingArray;
		const int32 BindingLimit = MaxBindings <= 0 ? Bindings.Num() : FMath::Min(MaxBindings, Bindings.Num());
		for (int32 BindingIndex = 0; BindingIndex < BindingLimit; ++BindingIndex)
		{
			const FMovieSceneBinding& Binding = Bindings[BindingIndex];
			TSharedPtr<FJsonObject> BindingObject = MakeShareable(new FJsonObject);
			const FGuid& BindingGuid = Binding.GetObjectGuid();
			BindingObject->SetStringField(TEXT("guid"), BindingGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			BindingObject->SetStringField(TEXT("name"), ResolveBindingName(MovieScene, BindingGuid));
			BindingObject->SetStringField(TEXT("binding_type"), ResolveBindingType(MovieScene, BindingGuid));
			BindingObject->SetNumberField(TEXT("track_count"), Binding.GetTracks().Num());

			TArray<TSharedPtr<FJsonValue>> BindingTrackArray;
			const TArray<UMovieSceneTrack*>& Tracks = Binding.GetTracks();
			const int32 TrackLimit = MaxTracks <= 0 ? Tracks.Num() : FMath::Min(MaxTracks, Tracks.Num());
			for (int32 TrackIndex = 0; TrackIndex < TrackLimit; ++TrackIndex)
			{
				BindingTrackArray.Add(MakeShareable(new FJsonValueObject(SerializeTrack(Tracks[TrackIndex], MaxSections))));
			}
			BindingObject->SetArrayField(TEXT("tracks"), BindingTrackArray);
			BindingObject->SetBoolField(TEXT("tracks_truncated"), TrackLimit < Tracks.Num());
			BindingArray.Add(MakeShareable(new FJsonValueObject(BindingObject)));
		}
		Result->SetArrayField(TEXT("bindings"), BindingArray);
		Result->SetBoolField(TEXT("bindings_truncated"), BindingLimit < Bindings.Num());
	}

	return FMcpToolResult::StructuredSuccess(Result, TEXT("Level Sequence summary ready"));
}
