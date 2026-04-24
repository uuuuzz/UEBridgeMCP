// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Sequencer/EditSequencerTracksTool.h"

#include "Tools/Level/LevelActorToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Animation/AnimSequenceBase.h"
#include "LevelSequence.h"
#include "Misc/FrameNumber.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "MovieSceneSpawnable.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "ScopedTransaction.h"

namespace
{
	bool TryReadVectorField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() < 3)
		{
			return false;
		}

		OutVector.X = static_cast<float>((*Values)[0]->AsNumber());
		OutVector.Y = static_cast<float>((*Values)[1]->AsNumber());
		OutVector.Z = static_cast<float>((*Values)[2]->AsNumber());
		return true;
	}

	TRange<FFrameNumber> MakeFrameRange(int32 StartFrame, int32 EndFrame)
	{
		const int32 SafeEndFrame = EndFrame > StartFrame ? EndFrame : (StartFrame + 1);
		return TRange<FFrameNumber>(
			TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(StartFrame)),
			TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(SafeEndFrame)));
	}

	FString NormalizeTrackType(const FString& Value)
	{
		return Value.ToLower();
	}

	FGuid FindOrCreateBinding(ULevelSequence* Sequence, UMovieScene* MovieScene, AActor* Actor, UObject* Context, bool bDryRun, bool& bOutCreated)
	{
		bOutCreated = false;
		if (!Sequence || !MovieScene || !Actor)
		{
			return FGuid();
		}

		FGuid BindingGuid;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		BindingGuid = Sequence->FindBindingFromObject(Actor, Context);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (BindingGuid.IsValid())
		{
			return BindingGuid;
		}

		if (bDryRun)
		{
			bOutCreated = true;
			return FGuid::NewGuid();
		}

		BindingGuid = MovieScene->AddPossessable(Actor->GetActorNameOrLabel(), Actor->GetClass());
		Sequence->BindPossessableObject(BindingGuid, *Actor, Context);
		bOutCreated = true;
		return BindingGuid;
	}

	template<typename TrackType>
	TrackType* FindOrAddTrack(UMovieScene* MovieScene, const FGuid& BindingGuid, bool bDryRun)
	{
		if (!MovieScene || !BindingGuid.IsValid())
		{
			return nullptr;
		}

		if (TrackType* ExistingTrack = MovieScene->FindTrack<TrackType>(BindingGuid))
		{
			return ExistingTrack;
		}

		if (bDryRun)
		{
			return nullptr;
		}

		return MovieScene->AddTrack<TrackType>(BindingGuid);
	}

	template<typename TrackType>
	TrackType* FindPropertyTrack(UMovieScene* MovieScene, const FGuid& BindingGuid, const FString& PropertyName, const FString& PropertyPath)
	{
		if (!MovieScene || !BindingGuid.IsValid())
		{
			return nullptr;
		}

		for (UMovieSceneTrack* Track : MovieScene->FindTracks(TrackType::StaticClass(), BindingGuid))
		{
			TrackType* PropertyTrack = Cast<TrackType>(Track);
			const UMovieScenePropertyTrack* TypedTrack = Cast<UMovieScenePropertyTrack>(Track);
			if (!PropertyTrack || !TypedTrack)
			{
				continue;
			}

			if (TypedTrack->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase) &&
				(PropertyPath.IsEmpty() || TypedTrack->GetPropertyPath().ToString().Equals(PropertyPath, ESearchCase::IgnoreCase)))
			{
				return PropertyTrack;
			}
		}

		return nullptr;
	}

	template<typename TrackType>
	TrackType* FindOrAddPropertyTrack(UMovieScene* MovieScene, const FGuid& BindingGuid, const FString& PropertyName, const FString& PropertyPath, bool bDryRun)
	{
		if (TrackType* Existing = FindPropertyTrack<TrackType>(MovieScene, BindingGuid, PropertyName, PropertyPath))
		{
			return Existing;
		}

		if (!MovieScene || !BindingGuid.IsValid() || bDryRun)
		{
			return nullptr;
		}

		TrackType* NewTrack = MovieScene->AddTrack<TrackType>(BindingGuid);
		if (NewTrack)
		{
			NewTrack->SetPropertyNameAndPath(FName(*PropertyName), PropertyPath.IsEmpty() ? PropertyName : PropertyPath);
			NewTrack->SetDisplayName(FText::FromString(PropertyName));
		}
		return NewTrack;
	}

	template<typename SectionType, typename TrackType>
	SectionType* FindOrCreateSection(TrackType* Track, const TRange<FFrameNumber>& Range, bool bDryRun)
	{
		if (!Track)
		{
			return nullptr;
		}

		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (Section && Section->GetRange().Overlaps(Range))
			{
				return Cast<SectionType>(Section);
			}
		}

		if (bDryRun)
		{
			return nullptr;
		}

		SectionType* NewSection = Cast<SectionType>(Track->CreateNewSection());
		if (NewSection)
		{
			NewSection->SetRange(Range);
			Track->AddSection(*NewSection);
		}
		return NewSection;
	}

	void AddTransformKeys(UMovieScene3DTransformSection* Section, int32 Frame, const FVector* Location, const FVector* Rotation, const FVector* Scale)
	{
		if (!Section)
		{
			return;
		}

		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		if (Channels.Num() >= 9)
		{
			if (Location)
			{
				Channels[0]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Location->X));
				Channels[1]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Location->Y));
				Channels[2]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Location->Z));
			}
			if (Rotation)
			{
				Channels[3]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Rotation->X));
				Channels[4]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Rotation->Y));
				Channels[5]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Rotation->Z));
			}
			if (Scale)
			{
				Channels[6]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Scale->X));
				Channels[7]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Scale->Y));
				Channels[8]->GetData().AddKey(FFrameNumber(Frame), FMovieSceneDoubleValue(Scale->Z));
			}
		}
	}

	void SetBindingDisplayName(UMovieScene* MovieScene, const FGuid& BindingGuid, const FString& BindingName)
	{
		if (!MovieScene || !BindingGuid.IsValid() || BindingName.IsEmpty())
		{
			return;
		}

		if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
		{
			Possessable->SetName(BindingName);
			return;
		}

		if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingGuid))
		{
			Spawnable->SetName(BindingName);
		}
	}
}

FString UEditSequencerTracksTool::GetToolDescription() const
{
	return TEXT("Edit Level Sequence bindings, playback range, camera cuts, transform/property tracks, animation sections, and simple keyframes.");
}

TMap<FString, FMcpSchemaProperty> UEditSequencerTracksTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("LevelSequence asset path"), true));
	Schema.Add(TEXT("world"), FMcpSchemaProperty::MakeEnum(TEXT("World mode to resolve actor bindings"), { TEXT("editor"), TEXT("pie") }));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Sequencer edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Sequencer edit action"),
		{ TEXT("bind_actor"), TEXT("set_playback_range"), TEXT("add_camera_cut"), TEXT("add_transform_track"), TEXT("add_float_track"), TEXT("add_bool_track"), TEXT("add_animation_section"), TEXT("set_section_range"), TEXT("add_transform_key"), TEXT("add_float_key"), TEXT("add_bool_key") },
		true)));
	OperationSchema->Properties.Add(TEXT("actor_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target actor label or name"))));
	OperationSchema->Properties.Add(TEXT("actor_handle"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Target actor handle"))));
	OperationSchema->Properties.Add(TEXT("binding_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional display name for new bindings"))));
	OperationSchema->Properties.Add(TEXT("start_frame"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Start frame"))));
	OperationSchema->Properties.Add(TEXT("end_frame"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("End frame"))));
	OperationSchema->Properties.Add(TEXT("section_index"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Section index for set_section_range"))));
	OperationSchema->Properties.Add(TEXT("track_type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Track type for set_section_range"))));
	OperationSchema->Properties.Add(TEXT("property_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Property name for float/bool tracks"))));
	OperationSchema->Properties.Add(TEXT("property_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional property path for float/bool tracks"))));
	OperationSchema->Properties.Add(TEXT("animation_asset_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Animation asset path for add_animation_section"))));
	OperationSchema->Properties.Add(TEXT("slot_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional slot name for skeletal animation sections"))));
	OperationSchema->Properties.Add(TEXT("frame"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Frame number for key creation"))));
	OperationSchema->Properties.Add(TEXT("location"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Transform location [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("rotation"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Transform rotation [pitch,yaw,roll]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("scale"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeArray(TEXT("Transform scale [x,y,z]"), TEXT("number"))));
	OperationSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Float key value"))));
	OperationSchema->Properties.Add(TEXT("bool_value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Bool key value"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Sequencer edit operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the sequence asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on the first failure")));
	return Schema;
}

FMcpToolResult UEditSequencerTracksTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString RequestedWorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

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

	UWorld* World = FMcpAssetModifier::ResolveWorld(RequestedWorldType);
	if (!World)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_WORLD_NOT_FOUND"), TEXT("Unable to resolve the requested world"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeShareable(new FScopedTransaction(FText::FromString(TEXT("Edit Sequencer Tracks"))));
		FMcpAssetModifier::MarkModified(LevelSequence);
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		const FString Action = GetStringArgOrDefault(*OperationObject, TEXT("action")).ToLower();
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);
		ResultObject->SetBoolField(TEXT("changed"), false);

		auto ResolveActorBinding = [&](FGuid& OutBindingGuid, AActor*& OutActor, bool& bOutCreated) -> bool
		{
			UWorld* ResolvedWorld = nullptr;
			FString ErrorCode;
			FString ErrorMessage;
			TSharedPtr<FJsonObject> ErrorDetails;
			OutActor = LevelActorToolUtils::ResolveActorReference(
				*OperationObject,
				RequestedWorldType,
				TEXT("actor_name"),
				TEXT("actor_handle"),
				Context,
				ResolvedWorld,
				ErrorCode,
				ErrorMessage,
				ErrorDetails,
				true);
			if (!OutActor)
			{
				ResultObject->SetStringField(TEXT("error"), ErrorMessage);
				return false;
			}

			OutBindingGuid = FindOrCreateBinding(LevelSequence, MovieScene, OutActor, World, bDryRun, bOutCreated);
			if (!OutBindingGuid.IsValid())
			{
				ResultObject->SetStringField(TEXT("error"), TEXT("Failed to resolve or create a binding for the actor"));
				return false;
			}

			ResultObject->SetStringField(TEXT("actor_name"), OutActor->GetActorNameOrLabel());
			ResultObject->SetStringField(TEXT("binding_guid"), OutBindingGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
			return true;
		};

		if (Action == TEXT("set_playback_range"))
		{
			const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
			const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
			if (!bDryRun)
			{
				MovieScene->SetPlaybackRange(MakeFrameRange(StartFrame, EndFrame));
			}
			ResultObject->SetBoolField(TEXT("changed"), true);
			bAnyChanged = true;
		}
		else if (Action == TEXT("bind_actor"))
		{
			FGuid BindingGuid;
			AActor* Actor = nullptr;
			bool bCreated = false;
			if (ResolveActorBinding(BindingGuid, Actor, bCreated))
			{
				const FString BindingName = GetStringArgOrDefault(*OperationObject, TEXT("binding_name"));
				if (!bDryRun && bCreated && !BindingName.IsEmpty())
				{
					SetBindingDisplayName(MovieScene, BindingGuid, BindingName);
				}
				ResultObject->SetBoolField(TEXT("binding_created"), bCreated);
				ResultObject->SetBoolField(TEXT("changed"), bCreated);
				bAnyChanged |= bCreated;
			}
		}
		else if (Action == TEXT("add_camera_cut"))
		{
			FGuid BindingGuid;
			AActor* Actor = nullptr;
			bool bCreated = false;
			if (ResolveActorBinding(BindingGuid, Actor, bCreated))
			{
				const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
				const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
				if (!bDryRun)
				{
					UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
					if (!CameraCutTrack)
					{
						CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
					}
					if (CameraCutTrack)
					{
						CameraCutTrack->SetIsAutoManagingSections(false);
						const FMovieSceneObjectBindingID CameraBindingID = UE::MovieScene::FRelativeObjectBindingID(BindingGuid);
						UMovieSceneCameraCutSection* CutSection = CameraCutTrack->AddNewCameraCut(CameraBindingID, FFrameNumber(StartFrame));
						if (CutSection)
						{
							CutSection->SetRange(MakeFrameRange(StartFrame, EndFrame));
						}
					}
				}
				ResultObject->SetBoolField(TEXT("changed"), true);
				bAnyChanged = true;
			}
		}
		else if (Action == TEXT("add_transform_track") || Action == TEXT("add_transform_key"))
		{
			FGuid BindingGuid;
			AActor* Actor = nullptr;
			bool bCreated = false;
			if (ResolveActorBinding(BindingGuid, Actor, bCreated))
			{
				const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
				const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
				UMovieScene3DTransformTrack* TransformTrack = FindOrAddTrack<UMovieScene3DTransformTrack>(MovieScene, BindingGuid, bDryRun);
				if (!TransformTrack && !bDryRun)
				{
					ResultObject->SetStringField(TEXT("error"), TEXT("Failed to create transform track"));
				}
				else if (Action == TEXT("add_transform_track"))
				{
					if (!bDryRun)
					{
						FindOrCreateSection<UMovieScene3DTransformSection>(TransformTrack, MakeFrameRange(StartFrame, EndFrame), false);
					}
					ResultObject->SetBoolField(TEXT("changed"), true);
					bAnyChanged = true;
				}
				else
				{
					const int32 Frame = GetIntArgOrDefault(*OperationObject, TEXT("frame"), StartFrame);
					FVector Location;
					FVector Rotation;
					FVector Scale;
					const bool bHasLocation = TryReadVectorField(*OperationObject, TEXT("location"), Location);
					const bool bHasRotation = TryReadVectorField(*OperationObject, TEXT("rotation"), Rotation);
					const bool bHasScale = TryReadVectorField(*OperationObject, TEXT("scale"), Scale);

					if (!bHasLocation && !bHasRotation && !bHasScale)
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("At least one of 'location', 'rotation', or 'scale' is required for add_transform_key"));
					}
					else if (!bDryRun)
					{
						UMovieScene3DTransformSection* Section = FindOrCreateSection<UMovieScene3DTransformSection>(TransformTrack, MakeFrameRange(Frame, Frame + 1), false);
						AddTransformKeys(Section, Frame, bHasLocation ? &Location : nullptr, bHasRotation ? &Rotation : nullptr, bHasScale ? &Scale : nullptr);
					}

					if (!ResultObject->HasField(TEXT("error")))
					{
						ResultObject->SetBoolField(TEXT("changed"), true);
						bAnyChanged = true;
					}
				}
			}
		}
		else if (Action == TEXT("add_float_track") || Action == TEXT("add_float_key") || Action == TEXT("add_bool_track") || Action == TEXT("add_bool_key") || Action == TEXT("set_section_range"))
		{
			const FString PropertyName = GetStringArgOrDefault(*OperationObject, TEXT("property_name"));
			const FString PropertyPath = GetStringArgOrDefault(*OperationObject, TEXT("property_path"));
			const FString TrackType = NormalizeTrackType(GetStringArgOrDefault(*OperationObject, TEXT("track_type")));

			FGuid BindingGuid;
			AActor* Actor = nullptr;
			bool bCreated = false;
			if ((Action == TEXT("set_section_range") && TrackType == TEXT("camera_cut")) || ResolveActorBinding(BindingGuid, Actor, bCreated))
			{
				if (Action == TEXT("add_float_track") || Action == TEXT("add_float_key") || (Action == TEXT("set_section_range") && TrackType == TEXT("float")))
				{
					if (PropertyName.IsEmpty())
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("'property_name' is required for float track operations"));
					}
					else
					{
						UMovieSceneFloatTrack* FloatTrack = FindOrAddPropertyTrack<UMovieSceneFloatTrack>(MovieScene, BindingGuid, PropertyName, PropertyPath, bDryRun);
						if (!FloatTrack && !bDryRun)
						{
							ResultObject->SetStringField(TEXT("error"), TEXT("Failed to create float track"));
						}
						else if (Action == TEXT("add_float_track"))
						{
							const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
							const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
							if (!bDryRun)
							{
								FindOrCreateSection<UMovieSceneFloatSection>(FloatTrack, MakeFrameRange(StartFrame, EndFrame), false);
							}
							ResultObject->SetBoolField(TEXT("changed"), true);
							bAnyChanged = true;
						}
						else if (Action == TEXT("add_float_key"))
						{
							const int32 Frame = GetIntArgOrDefault(*OperationObject, TEXT("frame"), 0);
							if (!(*OperationObject)->HasField(TEXT("value")))
							{
								ResultObject->SetStringField(TEXT("error"), TEXT("'value' is required for add_float_key"));
							}
							else if (!bDryRun)
							{
								UMovieSceneFloatSection* Section = FindOrCreateSection<UMovieSceneFloatSection>(FloatTrack, MakeFrameRange(Frame, Frame + 1), false);
								Section->GetChannel().GetData().AddKey(FFrameNumber(Frame), FMovieSceneFloatValue(GetFloatArgOrDefault(*OperationObject, TEXT("value"), 0.0f)));
							}

							if (!ResultObject->HasField(TEXT("error")))
							{
								ResultObject->SetBoolField(TEXT("changed"), true);
								bAnyChanged = true;
							}
						}
						else
						{
							const int32 SectionIndex = FMath::Max(0, GetIntArgOrDefault(*OperationObject, TEXT("section_index"), 0));
							const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
							const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
							if (!FloatTrack->GetAllSections().IsValidIndex(SectionIndex))
							{
								ResultObject->SetStringField(TEXT("error"), TEXT("Section index out of range for float track"));
							}
							else if (!bDryRun)
							{
								FloatTrack->GetAllSections()[SectionIndex]->SetRange(MakeFrameRange(StartFrame, EndFrame));
							}

							if (!ResultObject->HasField(TEXT("error")))
							{
								ResultObject->SetBoolField(TEXT("changed"), true);
								bAnyChanged = true;
							}
						}
					}
				}
				else if (Action == TEXT("add_bool_track") || Action == TEXT("add_bool_key") || (Action == TEXT("set_section_range") && TrackType == TEXT("bool")))
				{
					if (PropertyName.IsEmpty())
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("'property_name' is required for bool track operations"));
					}
					else
					{
						UMovieSceneBoolTrack* BoolTrack = FindOrAddPropertyTrack<UMovieSceneBoolTrack>(MovieScene, BindingGuid, PropertyName, PropertyPath, bDryRun);
						if (!BoolTrack && !bDryRun)
						{
							ResultObject->SetStringField(TEXT("error"), TEXT("Failed to create bool track"));
						}
						else if (Action == TEXT("add_bool_track"))
						{
							const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
							const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
							if (!bDryRun)
							{
								FindOrCreateSection<UMovieSceneBoolSection>(BoolTrack, MakeFrameRange(StartFrame, EndFrame), false);
							}
							ResultObject->SetBoolField(TEXT("changed"), true);
							bAnyChanged = true;
						}
						else if (Action == TEXT("add_bool_key"))
						{
							const int32 Frame = GetIntArgOrDefault(*OperationObject, TEXT("frame"), 0);
							bool bBoolValue = false;
							if (!(*OperationObject)->TryGetBoolField(TEXT("bool_value"), bBoolValue))
							{
								ResultObject->SetStringField(TEXT("error"), TEXT("'bool_value' is required for add_bool_key"));
							}
							else if (!bDryRun)
							{
								UMovieSceneBoolSection* Section = FindOrCreateSection<UMovieSceneBoolSection>(BoolTrack, MakeFrameRange(Frame, Frame + 1), false);
								Section->GetChannel().GetData().AddKey(FFrameNumber(Frame), bBoolValue);
							}

							if (!ResultObject->HasField(TEXT("error")))
							{
								ResultObject->SetBoolField(TEXT("changed"), true);
								bAnyChanged = true;
							}
						}
						else
						{
							const int32 SectionIndex = FMath::Max(0, GetIntArgOrDefault(*OperationObject, TEXT("section_index"), 0));
							const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
							const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
							if (!BoolTrack->GetAllSections().IsValidIndex(SectionIndex))
							{
								ResultObject->SetStringField(TEXT("error"), TEXT("Section index out of range for bool track"));
							}
							else if (!bDryRun)
							{
								BoolTrack->GetAllSections()[SectionIndex]->SetRange(MakeFrameRange(StartFrame, EndFrame));
							}

							if (!ResultObject->HasField(TEXT("error")))
							{
								ResultObject->SetBoolField(TEXT("changed"), true);
								bAnyChanged = true;
							}
						}
					}
				}
				else if (Action == TEXT("set_section_range") && TrackType == TEXT("camera_cut"))
				{
					const int32 SectionIndex = FMath::Max(0, GetIntArgOrDefault(*OperationObject, TEXT("section_index"), 0));
					const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
					const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
					UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
					if (!CameraCutTrack || !CameraCutTrack->GetAllSections().IsValidIndex(SectionIndex))
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("Section index out of range for camera cut track"));
					}
					else if (!bDryRun)
					{
						CameraCutTrack->GetAllSections()[SectionIndex]->SetRange(MakeFrameRange(StartFrame, EndFrame));
					}

					if (!ResultObject->HasField(TEXT("error")))
					{
						ResultObject->SetBoolField(TEXT("changed"), true);
						bAnyChanged = true;
					}
				}
				else if (Action == TEXT("set_section_range") && TrackType == TEXT("animation"))
				{
					UMovieSceneSkeletalAnimationTrack* AnimationTrack = FindOrAddTrack<UMovieSceneSkeletalAnimationTrack>(MovieScene, BindingGuid, true);
					const int32 SectionIndex = FMath::Max(0, GetIntArgOrDefault(*OperationObject, TEXT("section_index"), 0));
					const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
					const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + 1);
					if (!AnimationTrack || !AnimationTrack->GetAllSections().IsValidIndex(SectionIndex))
					{
						ResultObject->SetStringField(TEXT("error"), TEXT("Section index out of range for animation track"));
					}
					else if (!bDryRun)
					{
						AnimationTrack->GetAllSections()[SectionIndex]->SetRange(MakeFrameRange(StartFrame, EndFrame));
					}

					if (!ResultObject->HasField(TEXT("error")))
					{
						ResultObject->SetBoolField(TEXT("changed"), true);
						bAnyChanged = true;
					}
				}
				else if (!ResultObject->HasField(TEXT("error")))
				{
					ResultObject->SetStringField(TEXT("error"), TEXT("Unsupported property track operation"));
				}
			}
		}
		else if (Action == TEXT("add_animation_section"))
		{
			FGuid BindingGuid;
			AActor* Actor = nullptr;
			bool bCreated = false;
			if (ResolveActorBinding(BindingGuid, Actor, bCreated))
			{
				const FString AnimationAssetPath = GetStringArgOrDefault(*OperationObject, TEXT("animation_asset_path"));
				FString AnimationLoadError;
				UAnimSequenceBase* AnimationAsset = FMcpAssetModifier::LoadAssetByPath<UAnimSequenceBase>(AnimationAssetPath, AnimationLoadError);
				if (!AnimationAsset)
				{
					ResultObject->SetStringField(TEXT("error"), AnimationLoadError);
				}
				else
				{
					const int32 StartFrame = GetIntArgOrDefault(*OperationObject, TEXT("start_frame"), 0);
					const int32 EndFrame = GetIntArgOrDefault(*OperationObject, TEXT("end_frame"), StartFrame + FMath::Max(1, FMath::RoundToInt(AnimationAsset->GetPlayLength() * 30.0f)));
					if (!bDryRun)
					{
						UMovieSceneSkeletalAnimationTrack* AnimationTrack = FindOrAddTrack<UMovieSceneSkeletalAnimationTrack>(MovieScene, BindingGuid, false);
						UMovieSceneSkeletalAnimationSection* Section = FindOrCreateSection<UMovieSceneSkeletalAnimationSection>(AnimationTrack, MakeFrameRange(StartFrame, EndFrame), false);
						Section->Params.Animation = AnimationAsset;
						const FString SlotName = GetStringArgOrDefault(*OperationObject, TEXT("slot_name"));
						if (!SlotName.IsEmpty())
						{
							Section->Params.SlotName = FName(*SlotName);
						}
						Section->SetRange(MakeFrameRange(StartFrame, EndFrame));
					}
					ResultObject->SetStringField(TEXT("animation_asset_path"), AnimationAssetPath);
					ResultObject->SetBoolField(TEXT("changed"), true);
					bAnyChanged = true;
				}
			}
		}
		else
		{
			ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported action: '%s'"), *Action));
		}

		if (ResultObject->HasField(TEXT("error")))
		{
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		if (bAnyFailed && bRollbackOnError)
		{
			break;
		}
	}

	if (!bDryRun && bAnyChanged)
	{
		FMcpAssetModifier::MarkPackageDirty(LevelSequence);
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueObject(McpV2ToolUtils::MakeAssetHandle(AssetPath, LevelSequence->GetClass()->GetName()))));
	}

	if (!bDryRun && bAnyChanged && bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(LevelSequence, false, SaveError))
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_SAVE_FAILED"), SaveError);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
