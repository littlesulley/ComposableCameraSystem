// Copyright 2026 Sulley. All Rights Reserved.

#include "Sequencer/ComposableCameraShotTrackEditor.h"

#include "ComposableCameraEditorStyle.h"
#include "ContentBrowserModule.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "MovieSceneTrack.h"
#include "MVVM/Views/ViewUtilities.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "SequencerSectionPainter.h"
#include "Styling/AppStyle.h"
#include "TimeToPixel.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FComposableCameraShotTrackEditor"

namespace
{
	/** True if `ObjectClass` is a `AComposableCameraLevelSequenceActor` (or
	 * subclass - notably `AComposableCameraLevelSequenceShotActor`). The
	 * binding-row menu entry only appears for LSActor bindings; bindings of
	 * any other class don't host a CCS internal camera and so a Shot track
	 * there is meaningless. */
	bool IsLSActorBinding(const UClass* ObjectClass)
	{
		return ObjectClass && ObjectClass->IsChildOf(AComposableCameraLevelSequenceActor::StaticClass());
	}

	/** Spawn a Shot section positioned at the Sequencer playhead with the
	 * Track's default 5-second duration. Same justification as the Patch
	 * helper: `FSequencerUtilities::CreateNewSection` mishandles
	 * "Infinite Key Areas" + doesn't reposition to the playhead. */
	UMovieSceneComposableCameraShotSection* CreateShotSectionAtPlayhead(UMovieSceneComposableCameraShotTrack* Track,
		const TSharedPtr<ISequencer>& Sequencer,
		int32 RowIndex)
	{
		if (!Track || !Sequencer.IsValid())
		{
			return nullptr;
		}

		UMovieSceneComposableCameraShotSection* NewSection =
			Cast<UMovieSceneComposableCameraShotSection>(Track->CreateNewSection());
		if (!NewSection)
		{
			return nullptr;
		}

		const FFrameNumber Playhead = Sequencer->GetLocalTime().Time.FrameNumber;
		const TRange<FFrameNumber> SeedRange = NewSection->GetTrueRange();
		const FFrameNumber Length =
			SeedRange.HasUpperBound() && SeedRange.HasLowerBound()
				? SeedRange.GetUpperBoundValue() - SeedRange.GetLowerBoundValue()
				: FFrameNumber(0);
		if (Length.Value > 0)
		{
			NewSection->SetRange(TRange<FFrameNumber>(Playhead, Playhead + Length));
		}

		int32 OverlapPriority = 0;
		for (UMovieSceneSection* Existing: Track->GetAllSections())
		{
			OverlapPriority = FMath::Max(Existing->GetOverlapPriority() + 1, OverlapPriority);
		}
		NewSection->SetOverlapPriority(OverlapPriority);
		// Only stamp a row index when the caller actually nominated one. Passing
		// INDEX_NONE through to SetRowIndex stomps the section's default row 0
		// with -1; subsequent FTrackModel::ForceUpdate then crashes on
		// `PopulatedRows[-1] = true` (TrackModel.cpp ~line 182).
		if (RowIndex >= 0)
		{
			NewSection->SetRowIndex(RowIndex);
		}
		NewSection->SetBlendType(EMovieSceneBlendType::Absolute);

		Track->Modify();
		Track->AddSection(*NewSection);

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		Sequencer->EmptySelection();
		Sequencer->SelectSection(NewSection);
		Sequencer->ThrobSectionSelection();

		return NewSection;
	}

	/** Find an existing Shot track under `BindingGuid`, or null. Sequencer
	 * doesn't enforce per-binding track uniqueness, but reusing the binding's
	 * single Shot track keeps the outliner tidy and matches the
	 * Camera-Cut-Track / Camera-Shake-Track convention. */
	UMovieSceneComposableCameraShotTrack* FindShotTrackForBinding(UMovieScene* MovieScene, const FGuid& BindingGuid)
	{
		if (!MovieScene || !BindingGuid.IsValid())
		{
			return nullptr;
		}
		for (const FMovieSceneBinding& Binding: MovieScene->GetBindings())
		{
			if (Binding.GetObjectGuid() != BindingGuid)
			{
				continue;
			}
			for (UMovieSceneTrack* Track: Binding.GetTracks())
			{
				if (UMovieSceneComposableCameraShotTrack* ShotTrack = Cast<UMovieSceneComposableCameraShotTrack>(Track))
				{
					return ShotTrack;
				}
			}
		}
		return nullptr;
	}
}

// FComposableCameraShotTrackEditor 

TSharedRef<ISequencerTrackEditor> FComposableCameraShotTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer)
{
	return MakeShareable(new FComposableCameraShotTrackEditor(OwningSequencer));
}

FComposableCameraShotTrackEditor::FComposableCameraShotTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
	// Auto-delete the Transform Track that Sequencer adds by default when an
	// LSActor binding is created. The LSActor's CineCamera pose is owned by
	// the CCS evaluation pipeline; a Transform Track on the same binding
	// would silently override the per-tick `ProjectPoseToCineCamera` write.
	ActorAddedHandle = InSequencer->OnActorAddedToSequencer().AddRaw(this, &FComposableCameraShotTrackEditor::OnActorAddedToSequencer);
}

FComposableCameraShotTrackEditor::~FComposableCameraShotTrackEditor()
{
	if (TSharedPtr<ISequencer> Sequencer = GetSequencer())
	{
		if (ActorAddedHandle.IsValid())
		{
			Sequencer->OnActorAddedToSequencer().Remove(ActorAddedHandle);
		}
	}
}

FText FComposableCameraShotTrackEditor::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Composable Camera Shot");
}

bool FComposableCameraShotTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneComposableCameraShotTrack::StaticClass();
}

bool FComposableCameraShotTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	if (!InSequence)
	{
		return false;
	}
	const ETrackSupport TrackSupported = InSequence->IsTrackSupported(UMovieSceneComposableCameraShotTrack::StaticClass());
	return TrackSupported != ETrackSupport::NotSupported;
}

const FSlateBrush* FComposableCameraShotTrackEditor::GetIconBrush() const
{
	// No registered ShotAsset class icon yet - borrow the generic CameraCut
	// brush so the row has something readable.
	return FAppStyle::GetBrush("Sequencer.Tracks.CinematicShot");
}

void FComposableCameraShotTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (!IsLSActorBinding(ObjectClass))
	{
		return;
	}

	MenuBuilder.AddMenuEntry(LOCTEXT("AddShotTrack", "Composable Camera Shot Track"),
		LOCTEXT("AddShotTrackTooltip",
			"Add a track that drives Composable Camera Shots - each section "
			"pushes its Shot data into this LS Actor's CompositionFramingNode "
			"for the section's duration. Inline sections embed the Shot value-typed; "
			"AssetReference sections soft-ref a UComposableCameraShotAsset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.Tracks.CinematicShot"),
		FUIAction(FExecuteAction::CreateRaw(this, &FComposableCameraShotTrackEditor::HandleAddShotTrackMenuEntryExecute, ObjectBindings)));
}

TSharedPtr<SWidget> FComposableCameraShotTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencer();
	const int32 RowIndex = Params.TrackInsertRowIndex;
	auto OnClickedCallback = [Track, WeakSequencer, RowIndex]() -> FReply
	{
		if (UMovieSceneComposableCameraShotTrack* ShotTrack = Cast<UMovieSceneComposableCameraShotTrack>(Track))
		{
			FScopedTransaction Transaction(LOCTEXT("AddShotSection", "Add Composable Camera Shot Section"));
			CreateShotSectionAtPlayhead(ShotTrack, WeakSequencer.Pin(), RowIndex);
		}
		return FReply::Handled();
	};
	return UE::Sequencer::MakeAddButton(LOCTEXT("AddSection", "Section"),
		FOnClicked::CreateLambda(OnClickedCallback),
		Params.ViewModel);
}

TSharedRef<ISequencerSection> FComposableCameraShotTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check(SupportsType(SectionObject.GetOuter()->GetClass()));
	return MakeShareable(new FComposableCameraShotSectionInterface(SectionObject, GetSequencer()));
}

bool FComposableCameraShotTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	UComposableCameraShotAsset* ShotAsset = Cast<UComposableCameraShotAsset>(Asset);
	if (!ShotAsset || !TargetObjectGuid.IsValid())
	{
		return false;
	}

	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return false;
	}

	// Confirm the drop target is an LSActor binding - otherwise the Shot
	// track would be meaningless on that binding row. We can't read the
	// possessable's class without a Sequencer-side helper, so trust the
	// outliner's drop validation: the user dropped a ShotAsset onto a binding
	// that already accepts our track type, OR onto an arbitrary binding (in
	// which case we silently bail rather than create an orphan track).
	const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(TargetObjectGuid);
	const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(TargetObjectGuid);
	const UClass* TargetClass = Possessable ? Possessable->GetPossessedObjectClass()
		: (Spawnable && Spawnable->GetObjectTemplate() ? Spawnable->GetObjectTemplate()->GetClass() : nullptr);
	if (!IsLSActorBinding(TargetClass))
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddShotSection_Transaction",
		"Add Composable Camera Shot Section from Asset"));

	UMovieSceneComposableCameraShotTrack* Track = FindShotTrackForBinding(MovieScene, TargetObjectGuid);
	if (!Track)
	{
		MovieScene->Modify();
		Track = MovieScene->AddTrack<UMovieSceneComposableCameraShotTrack>(TargetObjectGuid);
		if (!Track)
		{
			return false;
		}
		Track->SetDisplayName(LOCTEXT("ShotTrackDefaultName", "Composable Camera Shot"));
	}

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	UMovieSceneComposableCameraShotSection* NewSection =
		CreateShotSectionAtPlayhead(Track, SequencerPtr, INDEX_NONE);
	if (!NewSection)
	{
		return false;
	}
	NewSection->Modify();
	NewSection->Source = EComposableCameraShotSource::AssetReference;
	NewSection->ShotAssetRef = ShotAsset;
	NewSection->RefreshShotOverridesFromSource();

	return true;
}

void FComposableCameraShotTrackEditor::HandleAddShotTrackMenuEntryExecute(TArray<FGuid> ObjectBindings)
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
	if (!FocusedMovieScene || FocusedMovieScene->IsReadOnly() || ObjectBindings.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddShotTrack_Transaction", "Add Composable Camera Shot Track"));
	FocusedMovieScene->Modify();

	for (const FGuid& BindingGuid: ObjectBindings)
	{
		if (!BindingGuid.IsValid())
		{
			continue;
		}

		// Reuse the binding's single Shot track if one already exists.
		UMovieSceneComposableCameraShotTrack* Track = FindShotTrackForBinding(FocusedMovieScene, BindingGuid);
		const bool bCreatedTrack = (Track == nullptr);
		if (bCreatedTrack)
		{
			Track = FocusedMovieScene->AddTrack<UMovieSceneComposableCameraShotTrack>(BindingGuid);
			if (!Track)
			{
				continue;
			}
			Track->SetDisplayName(LOCTEXT("ShotTrackDefaultName", "Composable Camera Shot"));
		}

		// Seed with one default Inline section at the playhead - matches the
		// CVar / Patch track UX where the row is immediately usable.
		if (TSharedPtr<ISequencer> SequencerPtr = GetSequencer())
		{
			CreateShotSectionAtPlayhead(Track, SequencerPtr, INDEX_NONE);
			if (bCreatedTrack)
			{
				SequencerPtr->OnAddTrack(Track, BindingGuid);
			}
		}
	}
}

void FComposableCameraShotTrackEditor::OnActorAddedToSequencer(AActor* Actor, FGuid ObjectGuid)
{
	if (!Actor || !Actor->IsA(AComposableCameraLevelSequenceActor::StaticClass()) || !ObjectGuid.IsValid())
	{
		return;
	}

	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return;
	}

	const FMovieSceneBinding* Binding = MovieScene->GetBindings().FindByPredicate(
		[ObjectGuid](const FMovieSceneBinding& B) { return B.GetObjectGuid() == ObjectGuid; });
	if (!Binding)
	{
		return;
	}

	// Snapshot first - RemoveTrack mutates the binding's track array and we
	// don't want to invalidate the iterator mid-walk. There can theoretically
	// be more than one Transform Track on a binding (engine doesn't enforce
	// uniqueness), so collect every one before removing.
	TArray<UMovieSceneTrack*> TransformTracks;
	for (UMovieSceneTrack* Track: Binding->GetTracks())
	{
		if (Track && Track->IsA(UMovieScene3DTransformTrack::StaticClass()))
		{
			TransformTracks.Add(Track);
		}
	}
	if (TransformTracks.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveDefaultTransformTrack",
		"Remove default Transform Track for Composable Camera Level Sequence Actor"));
	MovieScene->Modify();
	for (UMovieSceneTrack* Track: TransformTracks)
	{
		MovieScene->RemoveTrack(*Track);
	}

	if (TSharedPtr<ISequencer> Sequencer = GetSequencer())
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemRemoved);
	}
}

// FComposableCameraShotSectionInterface 

FComposableCameraShotSectionInterface::FComposableCameraShotSectionInterface(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
	: Section(InSection)
	, WeakSequencer(InSequencer)
{
}

UMovieSceneSection* FComposableCameraShotSectionInterface::GetSectionObject()
{
	return &Section;
}

FText FComposableCameraShotSectionInterface::GetSectionTitle() const
{
	const UMovieSceneComposableCameraShotSection* ShotSection = Cast<UMovieSceneComposableCameraShotSection>(&Section);
	if (!ShotSection)
	{
		return LOCTEXT("EmptyShotSection", "<empty>");
	}

	switch (ShotSection->Source)
	{
		case EComposableCameraShotSource::Inline:
		{
			const int32 NumTargets = ShotSection->InlineShot.Targets.Num();
			return FText::Format(NSLOCTEXT("ComposableCameraShotTrack", "InlineShotTitleFmt",
					"Inline Shot ({0} {0}|plural(one=target,other=targets))"),
				NumTargets);
		}

		case EComposableCameraShotSource::AssetReference:
		{
			// Soft-ptr -> use AssetName without forcing a load (live load happens
			// at evaluation time inside the TrackInstance, not at title paint).
			const FString AssetName = ShotSection->ShotAssetRef.GetAssetName();
			if (AssetName.IsEmpty())
			{
				return LOCTEXT("AssetRefNoAsset", "(no asset)");
			}
			return FText::FromString(AssetName);
		}
	}
	return LOCTEXT("EmptyShotSection", "<empty>");
}

FText FComposableCameraShotSectionInterface::GetSectionToolTip() const
{
	return GetSectionTitle();
}

int32 FComposableCameraShotSectionInterface::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	int32 LayerId = Painter.PaintSectionBackground();

	// For every other section on this Track that overlaps the painted
	// section in time, paint an amber tint band over the overlap window.
	// Conveys "this is a transition zone - both Shots' solvers run and
	// blend per the incoming section's EnterTransition" to designers at
	// a glance, without requiring them to inspect the Details panel.
	//
	// Both sides of an overlap pair render the band against each other,
	// so the band visibility is symmetric (designer sees the cue from
	// either section's perspective). The "owner" of the transition
	// (incoming side, higher RowIndex) is conveyed by which section's
	// EnterTransition is set in Details.
	const UMovieSceneComposableCameraShotSection* This =
		Cast<UMovieSceneComposableCameraShotSection>(&Section);
	if (!This)
	{
		return LayerId;
	}

	const UMovieSceneTrack* Track = This->GetTypedOuter<UMovieSceneTrack>();
	if (!Track)
	{
		return LayerId;
	}

	const TRange<FFrameNumber> ThisRange = This->GetTrueRange();
	if (ThisRange.IsEmpty()
		|| !ThisRange.HasLowerBound() || !ThisRange.HasUpperBound())
	{
		return LayerId;
	}

	const FTimeToPixel& TimeToPixel = Painter.GetTimeConverter();
	const FGeometry& SectionGeometry = Painter.SectionGeometry;
	const float SectionHeight = SectionGeometry.GetLocalSize().Y;
	if (SectionHeight <= 0.f)
	{
		return LayerId;
	}

	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	if (!WhiteBrush)
	{
		return LayerId;
	}

	// Amber tint at low opacity - distinct from default Sequencer track
	// colors (blues / greens) so the overlap zone reads as "transition"
	// rather than "another track region". Low alpha ensures the section's
	// authored content (title, future per-section overlays) stays legible.
	const FLinearColor OverlapTint(1.0f, 0.55f, 0.10f, 0.22f);

	// `FrameToPixel` returns coordinates in the section's local geometry
	// (verified against engine's BoolPropertySection / EventSection
	// painters - they pass FrameToPixel results straight to
	// SectionGeometry.ToPaintGeometry without offset translation). So the
	// overlap band's local-X is just FrameToPixel(OverlapStart..OverlapEnd).
	for (UMovieSceneSection* Other: Track->GetAllSections())
	{
		if (!Other || Other == This)
		{
			continue;
		}
		const TRange<FFrameNumber> OtherRange = Other->GetTrueRange();
		const TRange<FFrameNumber> Overlap =
			TRange<FFrameNumber>::Intersection(ThisRange, OtherRange);
		if (Overlap.IsEmpty()
			|| !Overlap.HasLowerBound() || !Overlap.HasUpperBound())
		{
			continue;
		}
		const FFrameNumber OverlapStart = Overlap.GetLowerBoundValue();
		const FFrameNumber OverlapEnd = Overlap.GetUpperBoundValue();
		if ((OverlapEnd - OverlapStart).Value <= 0)
		{
			// Touching ranges (zero-width intersection) - no band to draw.
			continue;
		}

		const float StartLocalX = TimeToPixel.FrameToPixel(OverlapStart);
		const float EndLocalX = TimeToPixel.FrameToPixel(OverlapEnd);
		const float Width = FMath::Max(EndLocalX - StartLocalX, 0.f);
		if (Width < 0.5f)
		{
			// Sub-pixel band - not worth drawing (and would cause Slate
			// micro-allocation noise).
			continue;
		}

		FSlateDrawElement::MakeBox(Painter.DrawElements,
			++LayerId,
			SectionGeometry.ToPaintGeometry(FVector2f(Width, SectionHeight),
				FSlateLayoutTransform(FVector2f(StartLocalX, 0.f))),
			WhiteBrush,
			ESlateDrawEffect::None,
			OverlapTint);
	}

	return LayerId;
}

namespace
{
	/** Display label for an FMovieSceneBinding row - falls back to "(unnamed)"
	 * when the binding has no Name (engine allows this, designer-renamed
	 * bindings always have one). */
	FText GetBindingDisplayLabel(const FMovieSceneBinding& Binding)
	{
		const FString Raw = Binding.GetName();
		if (Raw.IsEmpty())
		{
			return LOCTEXT("UnnamedBinding", "(unnamed binding)");
		}
		return FText::FromString(Raw);
	}

	/** Find or insert an override for the given TargetIndex. Modifies Section. */
	FComposableCameraShotTargetActorOverride& FindOrAddOverride(UMovieSceneComposableCameraShotSection& Section, int32 TargetIndex)
	{
		for (FComposableCameraShotTargetActorOverride& Existing: Section.TargetActorOverrides)
		{
			if (Existing.TargetIndex == TargetIndex)
			{
				return Existing;
			}
		}
		Section.TargetActorOverrides.AddDefaulted();
		FComposableCameraShotTargetActorOverride& New = Section.TargetActorOverrides.Last();
		New.TargetIndex = TargetIndex;
		return New;
	}

	/** Resolve the binding currently overriding TargetIndex (if any). */
	FGuid GetCurrentOverrideGuid(const UMovieSceneComposableCameraShotSection& Section, int32 TargetIndex)
	{
		for (const FComposableCameraShotTargetActorOverride& Existing: Section.TargetActorOverrides)
		{
			if (Existing.TargetIndex == TargetIndex)
			{
				return Existing.Binding.GetGuid();
			}
		}
		return FGuid();
	}

	/** Build a submenu listing sequence bindings as click-to-pick entries for
	 * the given TargetIndex. The first entry is "Clear" (removes any
	 * existing override on this index); the rest are bindings sorted by
	 * display name. The currently-selected binding gets a check-mark via
	 * `IsCheckedAction`. */
	void BuildBindingPickerSubmenu(FMenuBuilder& MenuBuilder,
		UMovieSceneComposableCameraShotSection* ShotSection, int32 TargetIndex)
	{
		if (!ShotSection)
		{
			return;
		}
		UMovieScene* MovieScene = ShotSection->GetTypedOuter<UMovieScene>();
		if (!MovieScene)
		{
			return;
		}

		const FGuid CurrentGuid = GetCurrentOverrideGuid(*ShotSection, TargetIndex);

		// Clear option - only enabled when there's something to clear.
		MenuBuilder.AddMenuEntry(LOCTEXT("ClearTargetBinding", "Clear binding"),
			LOCTEXT("ClearTargetBindingTooltip",
				"Remove the per-section binding override for this target. "
				"Falls back to the Shot's authored Actor (TSoftObjectPtr) at evaluation time."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ShotSection, TargetIndex]()
				{
					const FScopedTransaction Transaction(LOCTEXT("ClearBindingTx", "Clear Shot Target Binding"));
					ShotSection->Modify();
					ShotSection->TargetActorOverrides.RemoveAll(
						[TargetIndex](const FComposableCameraShotTargetActorOverride& O)
						{
							return O.TargetIndex == TargetIndex;
						});
				}),
				FCanExecuteAction::CreateLambda([ShotSection, TargetIndex]()
				{
					return GetCurrentOverrideGuid(*ShotSection, TargetIndex).IsValid();
				})));

		MenuBuilder.AddSeparator();

		// Snapshot bindings into a local array we can sort. Engine returns
		// them in scene-authoring order; sorting alphabetically makes the
		// menu skim-readable when there are many bindings.
		TArray<const FMovieSceneBinding*> Sorted;
		Sorted.Reserve(MovieScene->GetBindings().Num());
		for (const FMovieSceneBinding& B: MovieScene->GetBindings())
		{
			if (B.GetObjectGuid().IsValid())
			{
				Sorted.Add(&B);
			}
		}
		Sorted.Sort([](const FMovieSceneBinding& A, const FMovieSceneBinding& B)
		{
			return A.GetName() < B.GetName();
		});

		if (Sorted.Num() == 0)
		{
			MenuBuilder.AddMenuEntry(LOCTEXT("NoBindings", "(no bindings in this sequence)"),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction(),
					FCanExecuteAction::CreateLambda([]() { return false; })));
			return;
		}

		for (const FMovieSceneBinding* Binding: Sorted)
		{
			const FGuid BindingGuid = Binding->GetObjectGuid();
			const bool bIsCurrent = (BindingGuid == CurrentGuid);

			MenuBuilder.AddMenuEntry(GetBindingDisplayLabel(*Binding),
				FText::Format(LOCTEXT("BindingEntryTooltip",
					"Bind Target [{0}] to this Sequencer binding. The override is resolved at "
					"evaluation time through the running sequence instance - works for both "
					"Spawnables and Possessables."),
					FText::AsNumber(TargetIndex)),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([ShotSection, TargetIndex, BindingGuid]()
					{
						const FScopedTransaction Transaction(LOCTEXT("SetBindingTx", "Set Shot Target Binding"));
						ShotSection->Modify();
						FComposableCameraShotTargetActorOverride& Entry = FindOrAddOverride(*ShotSection, TargetIndex);
						Entry.Binding = UE::MovieScene::FRelativeObjectBindingID(BindingGuid);
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([bIsCurrent]() { return bIsCurrent; })),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
	}

	/** Build the "Set Enter Transition" submenu. Top "Clear" entry
	 * removes the current selection; embedded `FAssetPickerConfig` picker
	 * below lets the designer pick any `UComposableCameraTransitionDataAsset`
	 * in the project. Same shape as the LSComponent track editor's Camera
	 * Type Asset picker - keyboard-friendly + filterable.
	 *
	 * The picker writes the selection straight to `Section->EnterTransition`
	 * (soft-ref) inside an FScopedTransaction so the edit is undoable. */
	void BuildSetEnterTransitionSubmenu(FMenuBuilder& MenuBuilder,
		UMovieSceneComposableCameraShotSection* ShotSection)
	{
		if (!ShotSection)
		{
			return;
		}

		// Clear option - disabled when there's nothing to clear so the menu
		// reads as an idempotent no-op rather than a "soft" command.
		MenuBuilder.AddMenuEntry(LOCTEXT("ClearEnterTransition", "Clear"),
			LOCTEXT("ClearEnterTransitionTooltip",
				"Remove the EnterTransition assignment on this Shot Section. The "
				"section becomes a hard cut instead of pose-blending across "
				"the overlap window."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ShotSection]()
				{
					const FScopedTransaction Tx(LOCTEXT("ClearTransitionTx",
						"Clear Shot Section EnterTransition"));
					ShotSection->Modify();
					ShotSection->EnterTransition.Reset();
					FSlateApplication::Get().DismissAllMenus();
				}),
				FCanExecuteAction::CreateLambda([ShotSection]()
				{
					return !ShotSection->EnterTransition.IsNull();
				})));

		MenuBuilder.AddSeparator();

		// Embedded asset picker - same `FAssetPickerConfig` shape as
		// `FComposableCameraLevelSequenceComponentTrackEditor::AddCameraTypeAssetSubMenu`.
		// Filter to UComposableCameraTransitionDataAsset (recursive picks up
		// any future subclasses). Single-select; on-pick writes to the
		// section's soft-ref and dismisses the menu.
		FAssetPickerConfig PickerConfig;
		{
			PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda(
				[ShotSection](const FAssetData& Asset)
				{
					FSlateApplication::Get().DismissAllMenus();
					if (UComposableCameraTransitionDataAsset* TransitionAsset =
						Cast<UComposableCameraTransitionDataAsset>(Asset.GetAsset()))
					{
						const FScopedTransaction Tx(LOCTEXT("SetTransitionTx",
							"Set Shot Section EnterTransition"));
						ShotSection->Modify();
						ShotSection->EnterTransition = TransitionAsset;
					}
				});
			PickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateLambda(
				[ShotSection](const TArray<FAssetData>& Assets)
				{
					FSlateApplication::Get().DismissAllMenus();
					if (Assets.Num() == 0)
					{
						return;
					}
					if (UComposableCameraTransitionDataAsset* TransitionAsset =
						Cast<UComposableCameraTransitionDataAsset>(Assets[0].GetAsset()))
					{
						const FScopedTransaction Tx(LOCTEXT("SetTransitionTx_Enter",
							"Set Shot Section EnterTransition"));
						ShotSection->Modify();
						ShotSection->EnterTransition = TransitionAsset;
					}
				});
			PickerConfig.bAllowNullSelection = false;
			PickerConfig.bAddFilterUI = true;
			PickerConfig.bShowTypeInColumnView = false;
			PickerConfig.InitialAssetViewType = EAssetViewType::List;
			PickerConfig.SaveSettingsName = TEXT("ComposableCameraShotEnterTransitionPicker");
			PickerConfig.Filter.ClassPaths.Add(UComposableCameraTransitionDataAsset::StaticClass()->GetClassPathName());
			PickerConfig.Filter.bRecursiveClasses = true;
		}

		FContentBrowserModule& ContentBrowserModule =
			FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		const float WidthOverride = 400.f;
		const float HeightOverride = 350.f;
		TSharedRef<SBox> PickerWidget = SNew(SBox)
			.WidthOverride(WidthOverride)
			.HeightOverride(HeightOverride)
			[ContentBrowserModule.Get().CreateAssetPicker(PickerConfig)];

		MenuBuilder.AddWidget(PickerWidget, FText::GetEmpty(), true);
	}

	/** Resolve the soft-ref'd EnterTransition's display name without forcing
	 * a load - uses `GetAssetName()` which reads the path component. Returns
	 * "(none)" for null soft-ref. Kept terse for menu label compositing. */
	FText GetEnterTransitionLabel(const UMovieSceneComposableCameraShotSection& Section)
	{
		const FString AssetName = Section.EnterTransition.GetAssetName();
		if (AssetName.IsEmpty())
		{
			return LOCTEXT("EnterTransitionNoneLabel", "(none)");
		}
		return FText::FromString(AssetName);
	}

	/** Build the parent "Bind Target Actors" submenu - one row per target in
	 * the resolved Inline / AssetReference Shot, each opening a binding
	 * picker submenu. Empty Shot -> disabled placeholder. */
	void BuildBindTargetActorsSubmenu(FMenuBuilder& MenuBuilder,
		UMovieSceneComposableCameraShotSection* ShotSection)
	{
		if (!ShotSection)
		{
			return;
		}

		FComposableCameraShot Shot;
		if (!ShotSection->BuildEffectiveShotWithoutBindings(Shot) || Shot.Targets.Num() == 0)
		{
			MenuBuilder.AddMenuEntry(LOCTEXT("NoTargets",
					"(no targets - add targets in the Shot Editor first)"),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction(),
					FCanExecuteAction::CreateLambda([]() { return false; })));
			return;
		}

		UMovieScene* MovieScene = ShotSection->GetTypedOuter<UMovieScene>();

		for (int32 i = 0; i < Shot.Targets.Num(); ++i)
		{
			// Compose row label: "Target N" plus current binding name (or
			// "(no binding)") so the user sees current state at a glance
			// without expanding the submenu.
			FText CurrentLabel = LOCTEXT("NoBindingLabel", "(no binding)");
			if (MovieScene)
			{
				const FGuid CurrentGuid = GetCurrentOverrideGuid(*ShotSection, i);
				if (CurrentGuid.IsValid())
				{
					for (const FMovieSceneBinding& B: MovieScene->GetBindings())
					{
						if (B.GetObjectGuid() == CurrentGuid)
						{
							CurrentLabel = GetBindingDisplayLabel(B);
							break;
						}
					}
				}
			}

			MenuBuilder.AddSubMenu(FText::Format(LOCTEXT("TargetRowFmt", "Target {0}: {1}"),
					FText::AsNumber(i), CurrentLabel),
				FText::Format(LOCTEXT("TargetRowTooltip",
					"Bind the actor for Target [{0}] to a Sequencer binding. The override is "
					"resolved at evaluation time through the running sequence instance."),
					FText::AsNumber(i)),
				FNewMenuDelegate::CreateLambda([ShotSection, i](FMenuBuilder& SubMenuBuilder)
				{
					BuildBindingPickerSubmenu(SubMenuBuilder, ShotSection, i);
				}));
		}
	}
}

void FComposableCameraShotSectionInterface::BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding)
{
	UMovieSceneComposableCameraShotSection* ShotSection = Cast<UMovieSceneComposableCameraShotSection>(&Section);
	if (!ShotSection)
	{
		return;
	}

	MenuBuilder.BeginSection(TEXT("ComposableCameraShot"), LOCTEXT("ShotSection", "Composable Camera Shot"));

	MenuBuilder.AddMenuEntry(LOCTEXT("EditShotEntry", "Edit Shot..."),
		LOCTEXT("EditShotTooltip",
			"Open the Shot Editor on this section's Shot. For Inline sections, "
			"the editor binds to the section itself; for AssetReference sections, "
			"the editor edits section-local overrides seeded from the ShotAsset."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.Tracks.CinematicShot"),
		FUIAction(FExecuteAction::CreateLambda([ShotSection]()
		{
			FComposableCameraShotEditor::OpenForShotSection(ShotSection);
		})));

	MenuBuilder.AddSubMenu(LOCTEXT("BindTargetActorsEntry", "Bind Target Actors"),
		LOCTEXT("BindTargetActorsTooltip",
			"Override per-target Actor resolution with Sequencer bindings (Spawnable / Possessable). "
			"This lets a ShotAsset authored against placeholder actors drive the camera at runtime "
			"with the actual sequence-bound actors - the underlying ShotAsset / Inline data is not "
			"mutated."),
		FNewMenuDelegate::CreateLambda([ShotSection](FMenuBuilder& SubMenuBuilder)
		{
			BuildBindTargetActorsSubmenu(SubMenuBuilder, ShotSection);
		}));

	// EnterTransition picker. Label includes the current selection so the
	// parent menu reads "Set Enter Transition: <name>" without forcing the
	// user to expand the submenu just to check what's set.
	MenuBuilder.AddSubMenu(FText::Format(LOCTEXT("SetEnterTransitionEntryFmt", "Set Enter Transition: {0}"),
			GetEnterTransitionLabel(*ShotSection)),
		LOCTEXT("SetEnterTransitionTooltip",
			"Pick the transition asset that drives the inter-Shot blend when the playhead "
			"enters this Section from a previous overlapping Section on the same Shot Track. "
			"The overlap window itself defines blend duration; the transition asset contributes "
			"only its ease curve. Null = hard cut."),
		FNewMenuDelegate::CreateLambda([ShotSection](FMenuBuilder& SubMenuBuilder)
		{
			BuildSetEnterTransitionSubmenu(SubMenuBuilder, ShotSection);
		}));

	MenuBuilder.EndSection();
}

#undef LOCTEXT_NAMESPACE
