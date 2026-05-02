// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneComposableCameraShotTrack.generated.h"

class UMovieSceneSection;

/**
 * Sequencer track that drives Composable Camera Shots — Phase E of
 * Shot-Based Keyframing.
 *
 * Each section on the track represents one Shot activation window in the
 * timeline. The active Shot's data (`FComposableCameraShot`) is pushed every
 * frame into the bound `AComposableCameraLevelSequenceActor`'s internal
 * `UComposableCameraCompositionFramingNode::Shot` UPROPERTY by the
 * `UMovieSceneComposableCameraShotTrackInstance` — so the runtime CCS pipeline
 * runs unchanged (TickCamera evaluates the framing node, the solver builds a
 * pose, the LS Component projects it to the CineCamera).
 *
 * Track binding model
 * ───────────────────
 * Bound under an `AComposableCameraLevelSequenceActor` (or subclass —
 * notably the Phase E `AComposableCameraLevelSequenceShotActor`) binding row,
 * NOT root-level. The track has no `TargetActorBinding` field — its parent in
 * the outliner is the binding it drives. The track editor (Phase E.4)
 * surfaces the menu entry only when the binding's class matches.
 *
 * Multi-row + overlap semantics (V1)
 * ──────────────────────────────────
 * Rows enabled. When multiple sections overlap on different rows the **top
 * row wins** (lowest row index → highest priority — Sequencer's standard
 * ordering, mirrors Camera Cut). Phase F will reinterpret overlap as a
 * transition zone with multi-Shot blending at the CompositionFramingNode
 * level; the LSComponent's shot-override map is already designed to hold
 * multiple active entries to support this.
 *
 * Phase F outlook
 * ───────────────
 * Easing is currently disabled — Phase F adds inter-Shot CCS Transitions on
 * the section level, at which point easing handles drive the transition's
 * blend window. V1 sections are hard-cut.
 *
 * Section exit semantics
 * ──────────────────────
 * `CompositionFramingNode::Shot` retains the last-written value when no
 * section is active (gap between sections / past the final section). This is
 * intentional — the camera holds its last framing rather than snapping back
 * to a default. Designers explicitly add a new section to change the
 * framing.
 *
 * Modeled on `UMovieSceneComposableCameraPatchTrack` for layout consistency.
 * Shot-track-specific divergences:
 *   - Bound (under a binding row), not root.
 *   - No easing in V1.
 *   - No `TargetActorBinding` (the bound actor IS the parent binding).
 */
UCLASS(MinimalAPI)
class UMovieSceneComposableCameraShotTrack : public UMovieSceneNameableTrack
{
	GENERATED_BODY()

public:
	UMovieSceneComposableCameraShotTrack(const FObjectInitializer& ObjectInitializer);

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
