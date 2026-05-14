// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneComposableCameraPatchTrack.generated.h"

class UMovieSceneSection;

/**
 * Root-level Sequencer track that drives Composable Camera Patches.
 *
 * Each section on the track represents one Patch activation window (asset +
 * params + parameter bag); when a section enters its TrueRange the runtime
 * fires AddCameraPatch, when it exits ExpireCameraPatch fires.
 *
 * Root track (no object binding) by design -Patches live on the Director
 * resolved through PlayerIndex + ContextName on the section, mirroring the
 * BP `AddCameraPatch` library entry. This avoids forcing designers to bind
 * the track to the PCM (which is itself transient and hard to bind cleanly
 * in Sequencer); the section's own properties carry the addressing info.
 *
 * Multi-row support is on so designers can stack overlapping patches in the
 * same track (different LayerIndex per section, sorted by PatchManager). Each
 * section is independent. Sections do not blend with each other; the patch
 * compositor's LayerIndex order does. Easing on each section is enabled and
 * fed into the patch's envelope (EnterDuration / ExitDuration overrides) by
 * the TrackInstance.
 *
 * Modeled on UMovieSceneCVarTrack: same ImportEntityImpl-via-section pattern,
 * same per-section TrackInstance dispatch through the engine's
 * UMovieSceneTrackInstanceSystem. Track itself stores no animation data - sections own everything.
 */
UCLASS(MinimalAPI)
class UMovieSceneComposableCameraPatchTrack : public UMovieSceneNameableTrack
{
	GENERATED_BODY()

public:
	UMovieSceneComposableCameraPatchTrack(const FObjectInitializer& ObjectInitializer);

	// UMovieSceneTrack.
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual bool SupportsMultipleRows() const override { return true; }
	virtual EMovieSceneTrackEasingSupportFlags SupportsEasing(FMovieSceneSupportsEasingParams& Params) const override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool IsEmpty() const override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual void RemoveAllAnimationData() override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
