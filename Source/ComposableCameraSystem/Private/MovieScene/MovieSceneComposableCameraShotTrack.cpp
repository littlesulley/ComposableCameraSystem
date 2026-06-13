// Copyright 2026 Sulley. All Rights Reserved.

#include "MovieScene/MovieSceneComposableCameraShotTrack.h"

#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraShotTrack)

#define LOCTEXT_NAMESPACE "MovieSceneComposableCameraShotTrack"

UMovieSceneComposableCameraShotTrack::UMovieSceneComposableCameraShotTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Cool teal-green tint matches the ShotAsset Content Browser color and
	// distinguishes Shot rows from the warm orange Patch rows in the
	// Sequencer outliner. Alpha 65 = same translucency stock CCS / CVar
	// tracks use for their tint band.
	TrackTint = FColor(64, 192, 160, 65);
#endif

	// Pre/post-roll evaluation would push Shot overrides outside the authored
	// window and distort overlap-derived transition windows. Disable both.
	EvalOptions.bEvaluateInPreroll  = false;
	EvalOptions.bEvaluateInPostroll = false;
}

void UMovieSceneComposableCameraShotTrack::AddSection(UMovieSceneSection& Section)
{
	if (UMovieSceneComposableCameraShotSection* ShotSection = Cast<UMovieSceneComposableCameraShotSection>(&Section))
	{
		Sections.Add(ShotSection);
	}
}

bool UMovieSceneComposableCameraShotTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneComposableCameraShotSection::StaticClass();
}

UMovieSceneSection* UMovieSceneComposableCameraShotTrack::CreateNewSection()
{
	UMovieSceneComposableCameraShotSection* NewSection =
		NewObject<UMovieSceneComposableCameraShotSection>(this, NAME_None, RF_Transactional);

	UMovieScene* OwnerScene = GetTypedOuter<UMovieScene>();

	// Default 5-second window. Same convention as Patch / CameraShake. The
	// track editor repositions the section to the playhead before adding it.
	if (OwnerScene)
	{
		const FFrameRate TickResolution = OwnerScene->GetTickResolution();
		const FFrameTime FiveSeconds = 5.0 * TickResolution;
		NewSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FiveSeconds.FrameNumber + 1));
	}

	return NewSection;
}

EMovieSceneTrackEasingSupportFlags UMovieSceneComposableCameraShotTrack::SupportsEasing(FMovieSceneSupportsEasingParams& Params) const
{
	// Easing handles are disabled. Section overlap defines inter-Shot
	// transition duration instead.
	return EMovieSceneTrackEasingSupportFlags::None;
}

const TArray<UMovieSceneSection*>& UMovieSceneComposableCameraShotTrack::GetAllSections() const
{
	return Sections;
}

bool UMovieSceneComposableCameraShotTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

bool UMovieSceneComposableCameraShotTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

void UMovieSceneComposableCameraShotTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
}

void UMovieSceneComposableCameraShotTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
}

void UMovieSceneComposableCameraShotTrack::RemoveAllAnimationData()
{
	Sections.Empty();
}

#if WITH_EDITORONLY_DATA
FText UMovieSceneComposableCameraShotTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Composable Camera Shot");
}
#endif

#undef LOCTEXT_NAMESPACE
