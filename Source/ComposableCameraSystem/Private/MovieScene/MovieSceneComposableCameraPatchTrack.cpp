// Copyright 2026 Sulley. All Rights Reserved.

#include "MovieScene/MovieSceneComposableCameraPatchTrack.h"

#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraPatchSection.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraPatchTrack)

#define LOCTEXT_NAMESPACE "MovieSceneComposableCameraPatchTrack"

UMovieSceneComposableCameraPatchTrack::UMovieSceneComposableCameraPatchTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Warm-orange tint matches the Patch asset's Content Browser color
	// (#E08020). Keeps a visual through-line from the Content Browser
	// asset ->Sequencer track for the user. Alpha of 65 is the same
	// translucency stock CCS / CVar tracks use for their tint band.
	TrackTint = FColor(224, 128, 32, 65);
#endif

	// Patches handle their own pre/post envelopes via EnterDuration / ExitDuration.
	// Pre/post-roll evaluation would either fire AddPatch on cold context wakes
	// (preroll, before the section is conceptually "on") or hold the patch alive
	// past the section (postroll, doubling the exit ramp). Disable both.
	EvalOptions.bEvaluateInPreroll  = false;
	EvalOptions.bEvaluateInPostroll = false;
}

void UMovieSceneComposableCameraPatchTrack::AddSection(UMovieSceneSection& Section)
{
	if (UMovieSceneComposableCameraPatchSection* PatchSection = Cast<UMovieSceneComposableCameraPatchSection>(&Section))
	{
		Sections.Add(PatchSection);
	}
}

bool UMovieSceneComposableCameraPatchTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneComposableCameraPatchSection::StaticClass();
}

UMovieSceneSection* UMovieSceneComposableCameraPatchTrack::CreateNewSection()
{
	UMovieSceneComposableCameraPatchSection* NewSection =
		NewObject<UMovieSceneComposableCameraPatchSection>(this, NAME_None, RF_Transactional);

	UMovieScene* OwnerScene = GetTypedOuter<UMovieScene>();

	// UMovieSceneSection's default ctor sets SectionRange to TRange<FFrameNumber>(0)
	//. A degenerate one-frame range. Which renders as a near-invisible sliver
	// and can't be selected. Engine sections that don't want this either set
	// SetRange(All()) in their own ctor (CVar) or rely on InitialPlacement() at
	// the call site (CameraShake). Patches need a finite range (the section
	// range IS the patch's active window), so we give freshly-created sections
	// a 5-second default width. The track-editor / FSequencerUtilities side
	// then repositions them to the playhead and assigns row indices as needed.
	if (OwnerScene)
	{
		const FFrameRate TickResolution = OwnerScene->GetTickResolution();
		// Operator * (double, FFrameRate) ->FFrameTime; .FrameNumber rounds toward
		// zero. 5 seconds is the same default duration UMovieSceneCameraShakeTrack
		// uses in AddNewCameraShake.
		const FFrameTime FiveSeconds = 5.0 * TickResolution;
		NewSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FiveSeconds.FrameNumber + 1));
	}

	// Auto-bind TargetActorBinding when the sequence has exactly one
	// AComposableCameraLevelSequenceActor binding. Saves a manual picker step
	// for the common one-actor case (the typical Sequencer setup). Multiple LS
	// Actor bindings ->leave unset; designer disambiguates explicitly via the
	// Section's Details panel. Designer-set values aren't stomped (the field
	// starts unset on a fresh NewObject anyway, so this is a no-op when the
	// caller assigns the field after CreateNewSection returns).
	if (OwnerScene && !NewSection->TargetActorBinding.IsValid())
	{
		FGuid SoleCandidate;
		int32 MatchCount = 0;
		for (int32 i = 0; i < OwnerScene->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& Sp = OwnerScene->GetSpawnable(i);
			if (const UObject* Tpl = Sp.GetObjectTemplate())
			{
				if (Tpl->GetClass()->IsChildOf(AComposableCameraLevelSequenceActor::StaticClass()))
				{
					SoleCandidate = Sp.GetGuid();
					if (++MatchCount > 1) { break; }
				}
			}
		}
		if (MatchCount <= 1)
		{
#if WITH_EDITORONLY_DATA
			for (int32 i = 0; i < OwnerScene->GetPossessableCount(); ++i)
			{
				const FMovieScenePossessable& Po = OwnerScene->GetPossessable(i);
				if (const UClass* Cls = Po.GetPossessedObjectClass())
				{
					if (Cls->IsChildOf(AComposableCameraLevelSequenceActor::StaticClass()))
					{
						SoleCandidate = Po.GetGuid();
						if (++MatchCount > 1) { break; }
					}
				}
			}
#endif
		}
		if (MatchCount == 1)
		{
			// 5.6 doesn't expose FMovieSceneObjectBindingID(Guid, SequenceID) -			// construct via the UE::MovieScene::FRelativeObjectBindingID helper
			// (single-arg ctor defaults SequenceID to MovieSceneSequenceID::Root)
			// and let the converting constructor / operator= on FMovieSceneObjectBindingID
			// do the rest. This is the same path the Sequencer picker UI takes
			// when a designer drops a binding row onto the field.
			NewSection->TargetActorBinding = UE::MovieScene::FRelativeObjectBindingID(SoleCandidate);
		}
	}

	return NewSection;
}

EMovieSceneTrackEasingSupportFlags UMovieSceneComposableCameraPatchTrack::SupportsEasing(FMovieSceneSupportsEasingParams& Params) const
{
	// Easing handles on each section drive the Patch's enter/exit envelope when
	// the per-Params override bools are unset (TrackInstance reads
	// Section->Easing.GetEaseInDuration / GetEaseOutDuration). Allow both ends.
	return EMovieSceneTrackEasingSupportFlags::All;
}

const TArray<UMovieSceneSection*>& UMovieSceneComposableCameraPatchTrack::GetAllSections() const
{
	return Sections;
}

bool UMovieSceneComposableCameraPatchTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

bool UMovieSceneComposableCameraPatchTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

void UMovieSceneComposableCameraPatchTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
}

void UMovieSceneComposableCameraPatchTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
}

void UMovieSceneComposableCameraPatchTrack::RemoveAllAnimationData()
{
	Sections.Empty();
}

#if WITH_EDITORONLY_DATA
FText UMovieSceneComposableCameraPatchTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Composable Camera Patch");
}
#endif

#undef LOCTEXT_NAMESPACE
