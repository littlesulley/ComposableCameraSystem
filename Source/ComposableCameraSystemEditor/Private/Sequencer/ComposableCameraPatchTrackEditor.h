// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/KeyHandle.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "ISequencerTrackEditor.h"
#include "Misc/FrameNumber.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTrackEditor.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SWidget.h"

class FMenuBuilder;
class UMovieSceneComposableCameraPatchSection;
class UMovieSceneComposableCameraPatchTrack;

/**
 * Sequencer track editor for UMovieSceneComposableCameraPatchTrack.
 *
 * Surfaces three things in the editor:
 * 1. **Add Track menu entry** - root menu adds a "Composable Camera Patch
 * Track" entry (BuildAddTrackMenu). Clicking creates an empty track
 * with no section so the designer can immediately drag a section onto
 * a row.
 * 2. **Section + button on the outliner row** - BuildOutlinerEditWidget
 * returns a stock "+ Section" button (FSequencerUtilities::CreateNewSection)
 * so users add new patch windows without right-clicking.
 * 3. **Section interface** - MakeSectionInterface returns a thin
 * ISequencerSection that paints the section's title (= patch asset name)
 * and exposes "Camera Parameters" / "Camera Variables" submenus on
 * right-click. Clicking a leaf calls Sequencer->KeyProperty against the
 * section's Parameters/Variables bag, materializing a stock property
 * track on the path `Parameters.Value.{LeafName}` (or `Variables.Value.{LeafName}`).
 * Once the track exists, Sequencer's stock evaluation animates the bag's
 * backing FProperty in place each frame, and the runtime TrackInstance's
 * OnAnimate pushes those values through ApplyParameterBlockToActivePatch.
 *
 * Reuses the bag -> menu pattern from FComposableCameraLevelSequenceComponentTrackEditor - 
 * the leaves are CPF_Edit | CPF_Interp at bag-build time so CanKeyProperty
 * accepts them; without that flag the menu silently collapses (TechDoc Section 7.2
 * gotcha).
 *
 * SupportsType returns true ONLY for UMovieSceneComposableCameraPatchTrack - 
 * we own that track type, unlike the LS Component track editor which is a
 * pure menu extender on stock property tracks.
 */
class FComposableCameraPatchTrackEditor: public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

	explicit FComposableCameraPatchTrackEditor(TSharedRef<ISequencer> InSequencer);

	// ISequencerTrackEditor.
	virtual FText GetDisplayName() const override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual const FSlateBrush* GetIconBrush() const override;
	/** Drag-and-drop entry: Content Browser drop of UComposableCameraPatchTypeAsset
	 * on the Sequencer creates (or reuses) a single patch track and adds a
	 * section starting at the playhead pre-bound to the dropped asset.
	 * TargetObjectGuid is ignored (patch track is unbound). */
	virtual bool HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid) override;

private:
	/** Add Track menu entry click handler - creates a new track in the focused
	 * movie scene and registers it via Sequencer->OnAddTrack. */
	void HandleAddPatchTrackMenuEntryExecute();
};

/**
 * ISequencerSection implementation for one patch section.
 *
 * Paints a one-line title (the patch asset name, or "<empty>" when null) and
 * extends the section right-click context menu with "Camera Parameters" /
 * "Camera Variables" entries listing keyable bag leaves.
 */
class FComposableCameraPatchSectionInterface: public ISequencerSection, public TSharedFromThis<FComposableCameraPatchSectionInterface>
{
public:
	FComposableCameraPatchSectionInterface(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer);

	// ISequencerSection.
	virtual UMovieSceneSection* GetSectionObject() override;
	virtual FText GetSectionTitle() const override;
	virtual FText GetSectionToolTip() const override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;
	virtual void BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding) override;

	// Custom resize: drag either edge -> section AND every keyframe scale
	// proportionally around the OPPOSITE (fixed) edge. Matches what the
	// engine triggers via Ctrl+drag (FDilateSection), but applied to plain
	// drag too so the section behaves like a stretchable clip. The opposite
	// edge stays anchored, the dragged edge moves to the cursor, and every
	// channel key position is recomputed as `Origin + (Key - Origin) * factor`
	// from a snapshot taken in BeginResizeSection. DilateSection() is also
	// overridden so Ctrl+drag updates the section range correctly (engine
	// pre-writes the new key times in FDilateSection but expects the section
	// interface to update its range).
	virtual void BeginResizeSection() override;
	virtual void ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeFrameNumber) override;
	virtual void BeginDilateSection() override;
	virtual void DilateSection(const TRange<FFrameNumber>& NewRange, float DilationFactor) override;

public:
	/** Per-channel key snapshot type - public so the.cpp's anonymous-namespace
	 * helpers (`SnapshotKeyData` / `ApplyDilation`) can name the type when
	 * taking it by reference. Held by value in `InitialKeyData` below. */
	struct FInitialChannelData
	{
		struct FMovieSceneChannel* Channel = nullptr;
		TArray<FFrameNumber> Times;
		TArray<FKeyHandle> Handles;
	};

private:
	/** One promotable parameter leaf collected from the section's PatchAsset.
	 * Carries the pin metadata needed to pick the right AddXxxParameterKey
	 * overload + seed an initial key from the bag's static value. */
	struct FParameterMenuEntry
	{
		FName Name;
		FText DisplayName;
		EComposableCameraPinType PinType = EComposableCameraPinType::Float;
		const class UScriptStruct* StructType = nullptr;
		const class UEnum* EnumType = nullptr;
		bool bIsVariable = false; // false = Parameters bag, true = Variables bag
		bool bAlreadyKeyed = false; // true -> menu shows "(keyed)" / disables add
	};

	/** Collect one entry per ExposedParameter (or ExposedVariable) on the
	 * section's PatchAsset that we can map to a UMovieSceneParameterSection
	 * channel kind. Marks each entry's `bAlreadyKeyed` based on whether a
	 * curve already exists on the section. */
	void GatherParameterEntries(bool bIsVariables, TArray<FParameterMenuEntry>& OutEntries) const;

	/** Promote a parameter to a keyable channel by calling the matching
	 * Section->Add<X>ParameterKey at the current playhead time, seeded with
	 * the bag's static value. UMovieSceneParameterSection auto-creates the
	 * named curve struct on first call and rebuilds its channel proxy. */
	void PromoteParameterToChannel(FParameterMenuEntry Entry);
	bool CanPromoteParameter(FParameterMenuEntry Entry) const;

	/** The section we paint and key against. Held by raw ref because ISequencerSection
	 * conventionally owns its section by reference (matches stock interfaces). */
	UMovieSceneSection& Section;
	TWeakPtr<ISequencer> WeakSequencer;

	/** Per-channel key snapshot captured in BeginResizeSection. Each ResizeSection
	 * call recomputes new key times from this stable baseline (compounding off
	 * the live channel times mid-drag would oscillate / drift). Cleared between
	 * drags by re-populating in BeginResizeSection. Type FInitialChannelData
	 * is declared in the public section above. */
	TArray<FInitialChannelData> InitialKeyData;

	/** Section range at the moment the resize drag began. Drives the dilation
	 * factor (NewSize / InitialSize) and pins the dilation origin. */
	TRange<FFrameNumber> InitialResizeRange;
};
