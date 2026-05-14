// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "ISequencerTrackEditor.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTrackEditor.h"
#include "Templates/SubclassOf.h"

class FMenuBuilder;
class UMovieSceneComposableCameraShotSection;
class UMovieSceneComposableCameraShotTrack;

/**
 * Sequencer track editor for `UMovieSceneComposableCameraShotTrack` - Phase E
 * of Shot-Based Keyframing.
 *
 * Surfaces:
 * 1. **Object binding row menu entry** - `BuildObjectBindingTrackMenu`
 * adds "Composable Camera Shot Track" to the binding's `+ Track` menu
 * ONLY when the binding's class is `AComposableCameraLevelSequenceActor`
 * (or a subclass - notably `AComposableCameraLevelSequenceShotActor`).
 * Clicking adds a new Shot track under that binding with one default
 * Inline section spawning at the playhead.
 * 2. **Section + button on the outliner row** - `BuildOutlinerEditWidget`
 * returns a "+ Section" button that creates new Inline sections at the
 * playhead.
 * 3. **Section interface** - `MakeSectionInterface` returns a thin
 * ISequencerSection that paints a section title formatted as
 * `"Inline Shot (N targets)"` for Inline sections or the ShotAsset's
 * name (or "(no asset)") for AssetReference sections.
 * 4. **ShotAsset drag-drop** - `HandleAssetAdded` creates an
 * AssetReference Section pre-bound to the dropped asset, on the Shot
 * track of the binding the user dropped on (must be an LSActor
 * binding; ignored otherwise).
 *
 * Mirrors `FComposableCameraPatchTrackEditor` minus the Patch-specific
 * envelope / bag-leaf-keying paths - Shot sections carry no keyable
 * channels in V1.
 */
class FComposableCameraShotTrackEditor: public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

	explicit FComposableCameraShotTrackEditor(TSharedRef<ISequencer> InSequencer);
	virtual ~FComposableCameraShotTrackEditor() override;

	// ISequencerTrackEditor.
	virtual FText GetDisplayName() const override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder,
		const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual const FSlateBrush* GetIconBrush() const override;

	/** Drag-and-drop entry: Content Browser drop of `UComposableCameraShotAsset`
	 * onto an LSActor binding row creates (or reuses) a Shot track under
	 * that binding and adds an AssetReference section pre-bound to the
	 * dropped asset. Unbound drops are ignored. */
	virtual bool HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid) override;

private:
	/** Click handler for the menu entry - adds a Shot track under the bound
	 * object and seeds it with one default Inline section at the playhead. */
	void HandleAddShotTrackMenuEntryExecute(TArray<FGuid> ObjectBindings);

	/** Hook handler for `ISequencer::OnActorAddedToSequencer`. When an
	 * `AComposableCameraLevelSequenceActor` (or subclass) is added as a
	 * binding, remove the auto-generated `UMovieScene3DTransformTrack` - 
	 * the LSActor's CineCamera pose is owned by the CCS evaluation pipeline
	 * (CompositionFramingNode->solver ->ProjectPoseToCineCamera), and a
	 * Sequencer-keyed Transform Track on the same binding fights that
	 * authority and visually pins the camera at world origin. */
	void OnActorAddedToSequencer(AActor* Actor, FGuid ObjectGuid);

	/** Lifetime: registered in the constructor against the OwningSequencer's
	 * delegate; unregistered in the destructor. */
	FDelegateHandle ActorAddedHandle;
};

/**
 * ISequencerSection implementation for one Shot section.
 *
 * Paints a one-line title that summarises the section's source:
 * - Inline -> `"Inline Shot (N targets)"` (count = `Shot.Targets.Num()`).
 * - AssetReference + valid asset -> asset's name.
 * - AssetReference + null asset -> `"(no asset)"`.
 */
class FComposableCameraShotSectionInterface: public ISequencerSection, public TSharedFromThis<FComposableCameraShotSectionInterface>
{
public:
	FComposableCameraShotSectionInterface(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer);

	// ISequencerSection.
	virtual UMovieSceneSection* GetSectionObject() override;
	virtual FText GetSectionTitle() const override;
	virtual FText GetSectionToolTip() const override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override;

	/** Right-click context menu - adds an "Edit Shot..." entry that opens
	 * the Shot Editor for this section. Replaces the V1 single-click /
	 * double-click auto-open path (the auto-open was easy to mistrigger
	 * during normal Sequencer interaction - selecting sections to drag,
	 * to delete, or just clicking on the timeline near them all popped
	 * the editor open). The right-click is explicit and discoverable. */
	virtual void BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding) override;

private:
	UMovieSceneSection& Section;
	TWeakPtr<ISequencer> WeakSequencer;
};
