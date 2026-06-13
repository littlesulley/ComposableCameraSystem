// Copyright 2026 Sulley. All Rights Reserved.

#include "Sequencer/ComposableCameraPatchTrackEditor.h"

#include "Channels/MovieSceneChannelEditorData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "ComposableCameraEditorStyle.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraPatchSection.h"
#include "MovieScene/MovieSceneComposableCameraPatchTrack.h"
#include "MVVM/Views/ViewUtilities.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneParameterSection.h"
#include "SequencerSectionPainter.h"
#include "SequencerUtilities.h"
#include "Styling/AppStyle.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "FComposableCameraPatchTrackEditor"

namespace
{
	/**
	 * Spawn a Patch section positioned at the Sequencer playhead with the
	 * Track's default 5-second duration. Replaces `FSequencerUtilities::CreateNewSection`
	 * for our flows because the engine helper has two issues for our use case:
	 *
	 * (a) When Sequencer's "Infinite Key Areas" project setting is on, it
	 * force-stomps the new section's range to `TRange::All()` *without*
	 * checking `GetSupportsInfiniteRange()` (only the menu-driven
	 * `PopulateMenu_CreateNewSection` overload checks the flag - the
	 * simpler `CreateNewSection` overload doesn't).
	 * (b) Doesn't reposition the section to the playhead - it preserves
	 * whatever range `Track->CreateNewSection()` produces, which for us
	 * is a fixed `[0, 5s]` window starting at frame zero. Result: the
	 * new section appears far from the user's current scrub position.
	 *
	 * Returns the new section so callers can apply post-creation tweaks
	 * (PatchAsset assignment in HandleAssetAdded, etc).
	 */
	UMovieSceneComposableCameraPatchSection* CreatePatchSectionAtPlayhead(UMovieSceneComposableCameraPatchTrack* Track,
		const TSharedPtr<ISequencer>& Sequencer,
		int32 RowIndex)
	{
		if (!Track || !Sequencer.IsValid())
		{
			return nullptr;
		}

		UMovieSceneComposableCameraPatchSection* NewSection =
			Cast<UMovieSceneComposableCameraPatchSection>(Track->CreateNewSection());
		if (!NewSection)
		{
			return nullptr;
		}

		// Reposition to playhead while preserving the Track-default 5s width
		// (CreateNewSection seeds [0, 5s]; we shift the range so its lower
		// bound matches the playhead frame).
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

		// Mirror the engine's overlap-priority + blend-type stamps that the
		// stock CreateNewSection helper would normally apply.
		int32 OverlapPriority = 0;
		for (UMovieSceneSection* Existing: Track->GetAllSections())
		{
			OverlapPriority = FMath::Max(Existing->GetOverlapPriority() + 1, OverlapPriority);
		}
		NewSection->SetOverlapPriority(OverlapPriority);
		// Only stamp a row index when the caller actually nominated one. Passing
		// INDEX_NONE through to SetRowIndex stomps the section's default row 0
		// with -1; subsequent FTrackModel::ForceUpdate then crashes on
		// `PopulatedRows[-1] = true` (TrackModel.cpp ~line 182). Same fix as
		// the Shot track editor.
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
}

// FComposableCameraPatchTrackEditor 

TSharedRef<ISequencerTrackEditor> FComposableCameraPatchTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer)
{
	return MakeShareable(new FComposableCameraPatchTrackEditor(OwningSequencer));
}

FComposableCameraPatchTrackEditor::FComposableCameraPatchTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

FText FComposableCameraPatchTrackEditor::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Composable Camera Patch");
}

bool FComposableCameraPatchTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneComposableCameraPatchTrack::StaticClass();
}

bool FComposableCameraPatchTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	if (!InSequence)
	{
		return false;
	}
	const ETrackSupport TrackSupported = InSequence->IsTrackSupported(UMovieSceneComposableCameraPatchTrack::StaticClass());
	// Default-enabled for any sequence that doesn't actively veto us - Level
	// Sequences are the primary host but the track has no LS-specific bind.
	return TrackSupported != ETrackSupport::NotSupported;
}

const FSlateBrush* FComposableCameraPatchTrackEditor::GetIconBrush() const
{
	// `Sequencer.Tracks.CameraShake` doesn't exist as a registered AppStyle
	// brush in 5.6 (only Audio / Event / Fade / CameraCut / CinematicShot /
	// Slomo / TimeWarp / Animation / Sub / LevelVisibility / DataLayer / CVar
	// have entries - see Engine/Source/Editor/EditorStyle/Private/StarshipStyle.cpp).
	// Use our own registered Patch asset class icon for visual consistency
	// with the Content Browser thumbnail. Brush name is set in
	// FComposableCameraEditorStyle::FComposableCameraEditorStyle().
	return FComposableCameraEditorStyle::Get()->GetBrush("ClassIcon.ComposableCameraPatchTypeAsset");
}

void FComposableCameraPatchTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(LOCTEXT("AddPatchTrack", "Composable Camera Patch Track"),
		LOCTEXT("AddPatchTrackTooltip",
			"Adds a track that drives Composable Camera Patches - each section "
			"adds a Patch on the configured player/context for the section's "
			"duration, with envelope parameters keyable in place."),
		// Same brush as GetIconBrush() - keeps the menu entry icon, the
		// outliner row icon, and the asset thumbnail visually unified.
		FSlateIcon(FComposableCameraEditorStyle::Get()->GetStyleSetName(), "ClassIcon.ComposableCameraPatchTypeAsset"),
		FUIAction(FExecuteAction::CreateRaw(this, &FComposableCameraPatchTrackEditor::HandleAddPatchTrackMenuEntryExecute)));
}

TSharedPtr<SWidget> FComposableCameraPatchTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencer();
	const int32 RowIndex = Params.TrackInsertRowIndex;
	auto OnClickedCallback = [Track, WeakSequencer, RowIndex]() -> FReply
	{
		// Use our own playhead-positioning helper instead of the engine's
		// FSequencerUtilities::CreateNewSection - see CreatePatchSectionAtPlayhead
		// header comment for the InfiniteKeyAreas / no-reposition issues that
		// motivated the local replacement.
		if (UMovieSceneComposableCameraPatchTrack* PatchTrack = Cast<UMovieSceneComposableCameraPatchTrack>(Track))
		{
			FScopedTransaction Transaction(LOCTEXT("AddPatchSection", "Add Composable Camera Patch Section"));
			CreatePatchSectionAtPlayhead(PatchTrack, WeakSequencer.Pin(), RowIndex);
		}
		return FReply::Handled();
	};
	return UE::Sequencer::MakeAddButton(LOCTEXT("AddSection", "Section"),
		FOnClicked::CreateLambda(OnClickedCallback),
		Params.ViewModel);
}

TSharedRef<ISequencerSection> FComposableCameraPatchTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check(SupportsType(SectionObject.GetOuter()->GetClass()));
	return MakeShareable(new FComposableCameraPatchSectionInterface(SectionObject, GetSequencer()));
}

bool FComposableCameraPatchTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	UComposableCameraPatchTypeAsset* PatchAsset = Cast<UComposableCameraPatchTypeAsset>(Asset);
	if (!PatchAsset)
	{
		return false;
	}

	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddPatchSection_Transaction",
		"Add Composable Camera Patch Section from Asset"));

	// Reuse the single patch track on this sequence. Multiple patch tracks would
	// be valid (Sequencer doesn't enforce uniqueness), but the runtime overlay
	// path sorts by LayerIndex regardless of track partition - one track per
	// sequence keeps the outliner tidy and matches how Camera Cuts / Camera
	// Shakes are organised.
	UMovieSceneComposableCameraPatchTrack* Track = nullptr;
	for (UMovieSceneTrack* T: MovieScene->GetTracks())
	{
		if (UMovieSceneComposableCameraPatchTrack* PatchTrack = Cast<UMovieSceneComposableCameraPatchTrack>(T))
		{
			Track = PatchTrack;
			break;
		}
	}
	const bool bCreatedTrack = (Track == nullptr);
	if (bCreatedTrack)
	{
		MovieScene->Modify();
		Track = MovieScene->AddTrack<UMovieSceneComposableCameraPatchTrack>();
		if (!Track)
		{
			return false;
		}
		Track->SetDisplayName(LOCTEXT("PatchTrackDefaultName", "Composable Camera Patch"));
	}

	// CreatePatchSectionAtPlayhead handles range positioning + overlap priority +
	// row index + Track->AddSection + Sequencer notify/select/throb. We just
	// stamp the dropped PatchAsset onto the returned section.
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	UMovieSceneComposableCameraPatchSection* Section =
		CreatePatchSectionAtPlayhead(Track, SequencerPtr, INDEX_NONE);
	if (!Section)
	{
		return false;
	}
	Section->Modify();
	Section->PatchAsset = PatchAsset;

	return true;
}

void FComposableCameraPatchTrackEditor::HandleAddPatchTrackMenuEntryExecute()
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
	if (!FocusedMovieScene || FocusedMovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddPatchTrack_Transaction", "Add Composable Camera Patch Track"));
	FocusedMovieScene->Modify();

	UMovieSceneComposableCameraPatchTrack* NewTrack = FocusedMovieScene->AddTrack<UMovieSceneComposableCameraPatchTrack>();
	if (!NewTrack)
	{
		return;
	}
	NewTrack->SetDisplayName(LOCTEXT("PatchTrackDefaultName", "Composable Camera Patch"));

	// Create one empty section by default so the track is immediately usable - 
	// matches the CVar track's UX. Designer picks the patch asset in the
	// Details panel; the section is a no-op until they do.
	// Routed through CreatePatchSectionAtPlayhead so the section spawns at
	// the user's current scrub position with finite duration (5 sec) instead
	// of the bare CreateNewSection's [0, 5s] window.
	if (TSharedPtr<ISequencer> SequencerPtr = GetSequencer())
	{
		CreatePatchSectionAtPlayhead(NewTrack, SequencerPtr, INDEX_NONE);
		SequencerPtr->OnAddTrack(NewTrack, FGuid());
	}
}

// FComposableCameraPatchSectionInterface 

FComposableCameraPatchSectionInterface::FComposableCameraPatchSectionInterface(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
	: Section(InSection)
	, WeakSequencer(InSequencer)
{
}

UMovieSceneSection* FComposableCameraPatchSectionInterface::GetSectionObject()
{
	return &Section;
}

FText FComposableCameraPatchSectionInterface::GetSectionTitle() const
{
	const UMovieSceneComposableCameraPatchSection* PatchSection = Cast<UMovieSceneComposableCameraPatchSection>(&Section);
	if (!PatchSection || !PatchSection->PatchAsset)
	{
		return LOCTEXT("EmptyPatchSection", "<empty>");
	}
	return FText::FromString(PatchSection->PatchAsset->GetName());
}

FText FComposableCameraPatchSectionInterface::GetSectionToolTip() const
{
	return GetSectionTitle();
}

int32 FComposableCameraPatchSectionInterface::OnPaintSection(FSequencerSectionPainter& Painter) const
{
	int32 LayerId = Painter.PaintSectionBackground();

	const UMovieSceneComposableCameraPatchSection* PatchSection = Cast<UMovieSceneComposableCameraPatchSection>(&Section);
	if (!PatchSection || !PatchSection->PatchAsset)
	{
		return LayerId;
	}

	const FSlateBrush* IconBrush = FComposableCameraEditorStyle::Get()->GetBrush(TEXT("ClassIcon.ComposableCameraPatchTypeAsset"));
	if (!IconBrush)
	{
		return LayerId;
	}

	// Inset icon at the section's left edge so the warm-orange Patch identity
	// reads at-a-glance, matching the Content Browser thumbnail / track row icon.
	// Sized to (SectionHeight - 4px padding), capped at 20px so very tall rows
	// (resized splitter) don't blow it up. The section title text Sequencer
	// renders separately sits above the section content area, so the icon
	// doesn't fight it for space.
	const FGeometry& SectionGeometry = Painter.SectionGeometry;
	const float SectionHeight = SectionGeometry.GetLocalSize().Y;
	const float IconSize = FMath::Min(SectionHeight - 4.f, 20.f);
	if (IconSize <= 0.f)
	{
		return LayerId;
	}
	const FVector2f IconPos(4.f, (SectionHeight - IconSize) * 0.5f);

	FSlateDrawElement::MakeBox(Painter.DrawElements,
		++LayerId,
		SectionGeometry.ToPaintGeometry(FVector2f(IconSize, IconSize),
			FSlateLayoutTransform(IconPos)),
		IconBrush,
		ESlateDrawEffect::None,
		FLinearColor::White);

	return LayerId;
}

namespace
{
	/** Snapshot every key (handle + time) on every channel exposed by the
	 * section's proxy. Each ResizeSection call recomputes new times from
	 * this baseline rather than from the live channel state - using live
	 * state would compound the drag delta frame-over-frame and oscillate
	 * the keys toward 0 / infinity. */
	void SnapshotKeyData(UMovieSceneSection* SectionObject,
		TArray<FComposableCameraPatchSectionInterface::FInitialChannelData>& OutData)
	{
		OutData.Reset();
		if (!SectionObject)
		{
			return;
		}
		for (const FMovieSceneChannelEntry& Entry: SectionObject->GetChannelProxy().GetAllEntries())
		{
			for (FMovieSceneChannel* Channel: Entry.GetChannels())
			{
				if (!Channel)
				{
					continue;
				}
				FComposableCameraPatchSectionInterface::FInitialChannelData ChannelData;
				ChannelData.Channel = Channel;
				Channel->GetKeys(TRange<FFrameNumber>::All(), &ChannelData.Times, &ChannelData.Handles);
				if (ChannelData.Handles.Num() > 0)
				{
					OutData.Add(MoveTemp(ChannelData));
				}
			}
		}
	}

	/** Apply `Origin + (InitialTime - Origin) * Factor` to each cached key
	 * and write the new times back via Channel->SetKeyTimes. Floor toward
	 * zero matches what the engine's FDilateSection does (line 430 in
	 * EditToolDragOperations.cpp). */
	void ApplyDilation(const TArray<FComposableCameraPatchSectionInterface::FInitialChannelData>& InitialKeyData,
		FFrameNumber Origin,
		double DilationFactor)
	{
		TArray<FFrameNumber> NewTimes;
		for (const FComposableCameraPatchSectionInterface::FInitialChannelData& ChannelData: InitialKeyData)
		{
			if (!ChannelData.Channel)
			{
				continue;
			}
			NewTimes.Reset(ChannelData.Times.Num());
			for (FFrameNumber InitialTime: ChannelData.Times)
			{
				const int32 Offset = (InitialTime - Origin).Value;
				const int32 ScaledOffset = FMath::FloorToInt(Offset * DilationFactor);
				NewTimes.Add(Origin + FFrameNumber(ScaledOffset));
			}
			ChannelData.Channel->SetKeyTimes(ChannelData.Handles, NewTimes);
		}
	}
}

void FComposableCameraPatchSectionInterface::BeginResizeSection()
{
	UMovieSceneSection* SectionObject = GetSectionObject();
	if (!SectionObject)
	{
		InitialResizeRange = TRange<FFrameNumber>::Empty();
		InitialKeyData.Reset();
		return;
	}
	InitialResizeRange = SectionObject->GetRange();
	SnapshotKeyData(SectionObject, InitialKeyData);
}

void FComposableCameraPatchSectionInterface::ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeFrameNumber)
{
	UMovieSceneSection* SectionObject = GetSectionObject();
	if (!SectionObject)
	{
		return;
	}

	// Need both bounds closed AND known initial range for dilation. Without
	// either, fall back to the engine's default "set bound" behavior - keeps
	// the patch resizable on a section with an open bound (rare for our use
	// case but defensive).
	const bool bCanDilate = InitialResizeRange.GetLowerBound().IsClosed()
		&& InitialResizeRange.GetUpperBound().IsClosed();

	if (!bCanDilate)
	{
		ISequencerSection::ResizeSection(ResizeMode, ResizeFrameNumber);
		return;
	}

	const FFrameNumber InitialLower = InitialResizeRange.GetLowerBoundValue();
	const FFrameNumber InitialUpper = InitialResizeRange.GetUpperBoundValue();
	const int32 InitialSizeTicks = (InitialUpper - InitialLower).Value;

	// Snapshot of zero size -> can't compute a meaningful factor; defer to default.
	if (InitialSizeTicks <= 0)
	{
		ISequencerSection::ResizeSection(ResizeMode, ResizeFrameNumber);
		return;
	}

	// Compute the new range with the same clamping the engine default uses
	// (leading edge <= trailing - 1, trailing edge >= leading), and from there
	// derive the new size + dilation origin (the FIXED edge).
	FFrameNumber NewLower = InitialLower;
	FFrameNumber NewUpper = InitialUpper;
	FFrameNumber Origin = InitialUpper;

	if (ResizeMode == ESequencerSectionResizeMode::SSRM_LeadingEdge)
	{
		// Leading edge moves; trailing edge anchored.
		const FFrameNumber MaxFrame = InitialUpper - 1;
		NewLower = FMath::Min(ResizeFrameNumber, MaxFrame);
		Origin = InitialUpper;
	}
	else
	{
		// Trailing edge moves; leading edge anchored.
		const FFrameNumber MinFrame = InitialLower + 1;
		NewUpper = FMath::Max(ResizeFrameNumber, MinFrame);
		Origin = InitialLower;
	}

	const int32 NewSizeTicks = (NewUpper - NewLower).Value;
	if (NewSizeTicks <= 0)
	{
		// Section collapsed to nothing - keep range update but don't dilate
		// (would divide by initial size and collapse keys to origin, not useful).
		ISequencerSection::ResizeSection(ResizeMode, ResizeFrameNumber);
		return;
	}

	const double DilationFactor = static_cast<double>(NewSizeTicks) / static_cast<double>(InitialSizeTicks);

	SectionObject->Modify();
	ApplyDilation(InitialKeyData, Origin, DilationFactor);

	// Update section range using the engine's default impl - handles bound
	// kind (Inclusive/Exclusive) correctly and stays consistent with other
	// section types' resize semantics for the bounds part.
	ISequencerSection::ResizeSection(ResizeMode, ResizeFrameNumber);
}

void FComposableCameraPatchSectionInterface::BeginDilateSection()
{
	// Snapshot for engine Ctrl+drag too - engine's FDilateSection rewrites
	// channel times itself but we still want to update our own range.
	UMovieSceneSection* SectionObject = GetSectionObject();
	InitialResizeRange = SectionObject ? SectionObject->GetRange() : TRange<FFrameNumber>::Empty();
}

void FComposableCameraPatchSectionInterface::DilateSection(const TRange<FFrameNumber>& NewRange, float DilationFactor)
{
	// Engine's FDilateSection has already rewritten channel key times by the
	// time it calls us - our job is purely to commit the new section range.
	// Without this override the empty default ISequencerSection::DilateSection
	// wouldn't update bounds and Ctrl+drag would leave the section size
	// unchanged while keys moved.
	UMovieSceneSection* SectionObject = GetSectionObject();
	if (!SectionObject)
	{
		return;
	}
	SectionObject->Modify();
	SectionObject->SetRange(NewRange);
}

void FComposableCameraPatchSectionInterface::BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding)
{
	TArray<FParameterMenuEntry> ParameterEntries;
	GatherParameterEntries(/*bIsVariables=*/false, ParameterEntries);

	TArray<FParameterMenuEntry> VariableEntries;
	GatherParameterEntries(/*bIsVariables=*/true, VariableEntries);

	if (ParameterEntries.Num() == 0 && VariableEntries.Num() == 0)
	{
		return;
	}

	// Two flat sections - "Camera Parameters" and "Camera Variables". One click
	// per leaf promotes that exposed param to a keyable channel on the section
	// (UMovieSceneParameterSection auto-creates the named curve struct + an
	// initial key at the current playhead time, seeded from the bag's static
	// value). Already-keyed entries show "(keyed)" suffix and are disabled - 
	// designer can clean them via the channel row's right-click "Delete"
	// (FParameterSection::RequestDeleteCategory).
	auto BuildSection = [this, &MenuBuilder](FName SectionId, const FText& Header, const TArray<FParameterMenuEntry>& Entries)
	{
		if (Entries.Num() == 0)
		{
			return;
		}
		MenuBuilder.BeginSection(SectionId, Header);
		for (const FParameterMenuEntry& Entry: Entries)
		{
			const FText DisplayName = Entry.DisplayName.IsEmpty() ? FText::FromName(Entry.Name) : Entry.DisplayName;
			const FText Label = Entry.bAlreadyKeyed
				? FText::Format(LOCTEXT("ParamKeyed", "{0} (keyed)"), DisplayName)
				: DisplayName;

			FUIAction AddAction(FExecuteAction::CreateSP(this, &FComposableCameraPatchSectionInterface::PromoteParameterToChannel, Entry),
				FCanExecuteAction::CreateSP(this, &FComposableCameraPatchSectionInterface::CanPromoteParameter, Entry));
			MenuBuilder.AddMenuEntry(Label,
				LOCTEXT("ParamPromoteTooltip",
					"Promote this parameter to a keyable channel on the section. "
					"An initial key at the current playhead time is seeded with "
					"the parameter's static bag value."),
				FSlateIcon(),
				AddAction);
		}
		MenuBuilder.EndSection();
	};

	BuildSection(TEXT("CameraParameters"), LOCTEXT("CameraParameters", "Camera Parameters"), ParameterEntries);
	BuildSection(TEXT("CameraVariables"), LOCTEXT("CameraVariables", "Camera Variables"), VariableEntries);
}

namespace
{
	/** Read the bag's static value for `Name` into typed locals, returning a
	 * pin-type-tagged value the AddXxxParameterKey overloads consume.
	 * Defaults to type-default if the bag has no entry. */
	template <typename T>
	T GetBagDefault(const FInstancedPropertyBag& Bag, FName Name);

	template<> float GetBagDefault<float>(const FInstancedPropertyBag& Bag, FName Name)
	{
		if (auto R = Bag.GetValueFloat(Name); R.HasValue()) { return R.GetValue(); }
		if (auto R = Bag.GetValueDouble(Name); R.HasValue()) { return static_cast<float>(R.GetValue()); }
		return 0.f;
	}
	template<> bool GetBagDefault<bool>(const FInstancedPropertyBag& Bag, FName Name)
	{
		if (auto R = Bag.GetValueBool(Name); R.HasValue()) { return R.GetValue(); }
		return false;
	}
	template<> FVector2D GetBagDefault<FVector2D>(const FInstancedPropertyBag& Bag, FName Name)
	{
		if (auto R = Bag.GetValueStruct<FVector2D>(Name); R.HasValue()) { return *R.GetValue(); }
		return FVector2D::ZeroVector;
	}
	template<> FVector GetBagDefault<FVector>(const FInstancedPropertyBag& Bag, FName Name)
	{
		if (auto R = Bag.GetValueStruct<FVector>(Name); R.HasValue()) { return *R.GetValue(); }
		return FVector::ZeroVector;
	}
	template<> FRotator GetBagDefault<FRotator>(const FInstancedPropertyBag& Bag, FName Name)
	{
		if (auto R = Bag.GetValueStruct<FRotator>(Name); R.HasValue()) { return *R.GetValue(); }
		return FRotator::ZeroRotator;
	}
	template<> FLinearColor GetBagDefault<FLinearColor>(const FInstancedPropertyBag& Bag, FName Name)
	{
		// Vector4 is keyed via UMovieSceneParameterSection's Color curves (RGBA quad).
		if (auto R = Bag.GetValueStruct<FVector4>(Name); R.HasValue())
		{
			const FVector4 V = *R.GetValue();
			return FLinearColor(V.X, V.Y, V.Z, V.W);
		}
		return FLinearColor::Black;
	}

	/** Map a CCS pin type to the UMovieSceneParameterSection channel kind we'll
	 * use to key it. Returns false for types that don't have a sensible channel
	 * representation (Int32 / Enum / Object / Actor / Name / Struct / Transform /
	 * Delegate) - those stay bag-only. */
	enum class EPatchChannelKind: uint8 { None, Scalar, Bool, Vector2D, Vector, Color };

	EPatchChannelKind PinTypeToChannelKind(EComposableCameraPinType PinType)
	{
		switch (PinType)
		{
			case EComposableCameraPinType::Float:
			case EComposableCameraPinType::Double: return EPatchChannelKind::Scalar;
			case EComposableCameraPinType::Bool: return EPatchChannelKind::Bool;
			case EComposableCameraPinType::Vector2D: return EPatchChannelKind::Vector2D;
			case EComposableCameraPinType::Vector3D:
			case EComposableCameraPinType::Rotator: return EPatchChannelKind::Vector;
			case EComposableCameraPinType::Vector4: return EPatchChannelKind::Color;
			default: return EPatchChannelKind::None;
		}
	}

	/** True if `Section` already has a channel curve for `Name` of any kind. */
	bool IsAlreadyKeyed(const UMovieSceneComposableCameraPatchSection& Section, FName Name)
	{
		for (const FScalarParameterNameAndCurve& C: Section.GetScalarParameterNamesAndCurves()) { if (C.ParameterName == Name) return true; }
		for (const FBoolParameterNameAndCurve& C: Section.GetBoolParameterNamesAndCurves()) { if (C.ParameterName == Name) return true; }
		for (const FVector2DParameterNameAndCurves& C: Section.GetVector2DParameterNamesAndCurves()) { if (C.ParameterName == Name) return true; }
		for (const FVectorParameterNameAndCurves& C: Section.GetVectorParameterNamesAndCurves()) { if (C.ParameterName == Name) return true; }
		for (const FColorParameterNameAndCurves& C: Section.GetColorParameterNamesAndCurves()) { if (C.ParameterName == Name) return true; }
		return false;
	}
}

void FComposableCameraPatchSectionInterface::GatherParameterEntries(bool bIsVariables, TArray<FParameterMenuEntry>& OutEntries) const
{
	UMovieSceneComposableCameraPatchSection* PatchSection = Cast<UMovieSceneComposableCameraPatchSection>(&Section);
	if (!PatchSection || !PatchSection->PatchAsset)
	{
		return;
	}

	const UComposableCameraPatchTypeAsset* Asset = PatchSection->PatchAsset;

	auto EmitFromExposedSurface = [&](FName Name, FText DisplayName, EComposableCameraPinType PinType,
	 const UScriptStruct* StructType, const UEnum* EnumType)
	{
		// Filter: only show params whose pin type maps to a
		// UMovieSceneParameterSection channel kind. Int32 / Enum / Object /
		// Name / Struct / Delegate stay bag-only.
		if (PinTypeToChannelKind(PinType) == EPatchChannelKind::None)
		{
			return;
		}
		FParameterMenuEntry Entry;
		Entry.Name = Name;
		Entry.DisplayName = DisplayName;
		Entry.PinType = PinType;
		Entry.StructType = StructType;
		Entry.EnumType = EnumType;
		Entry.bIsVariable = bIsVariables;
		Entry.bAlreadyKeyed = IsAlreadyKeyed(*PatchSection, Name);
		OutEntries.Add(Entry);
	};

	if (bIsVariables)
	{
		for (const FComposableCameraInternalVariable& Var: Asset->ExposedVariables)
		{
			if (!Var.VariableName.IsNone())
			{
				EmitFromExposedSurface(Var.VariableName, FText::FromName(Var.VariableName),
					Var.VariableType, Var.StructType, Var.EnumType);
			}
		}
	}
	else
	{
		for (const FComposableCameraExposedParameter& Param: Asset->ExposedParameters)
		{
			if (!Param.ParameterName.IsNone())
			{
				EmitFromExposedSurface(Param.ParameterName, FText::FromName(Param.ParameterName),
					Param.PinType, Param.StructType, Param.EnumType);
			}
		}
	}

	OutEntries.Sort([](const FParameterMenuEntry& A, const FParameterMenuEntry& B)
	{
		return A.Name.LexicalLess(B.Name);
	});
}

void FComposableCameraPatchSectionInterface::PromoteParameterToChannel(FParameterMenuEntry Entry)
{
	TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();
	UMovieSceneComposableCameraPatchSection* PatchSection = Cast<UMovieSceneComposableCameraPatchSection>(&Section);
	if (!SequencerPtr.IsValid() || !PatchSection)
	{
		return;
	}

	const FFrameNumber CurrentFrame = SequencerPtr->GetLocalTime().Time.FloorToFrame();
	const FInstancedPropertyBag& Bag = Entry.bIsVariable ? PatchSection->Variables: PatchSection->Parameters;

	const FScopedTransaction Transaction(LOCTEXT("PromoteParamToChannel", "Promote parameter to keyable channel"));
	PatchSection->Modify();

	// Dispatch to the right AddXxxParameterKey overload based on pin type.
	// Each AddXxxParameterKey on UMovieSceneParameterSection auto-creates the
	// named curve struct on first call (FScalarParameterNameAndCurve / etc.)
	// and reconstructs the section's channel proxy so the new row appears in
	// Sequencer immediately.
	switch (PinTypeToChannelKind(Entry.PinType))
	{
		case EPatchChannelKind::Scalar:
			PatchSection->AddScalarParameterKey(Entry.Name, CurrentFrame, GetBagDefault<float>(Bag, Entry.Name));
			break;
		case EPatchChannelKind::Bool:
			PatchSection->AddBoolParameterKey(Entry.Name, CurrentFrame, GetBagDefault<bool>(Bag, Entry.Name));
			break;
		case EPatchChannelKind::Vector2D:
			PatchSection->AddVector2DParameterKey(Entry.Name, CurrentFrame, GetBagDefault<FVector2D>(Bag, Entry.Name));
			break;
		case EPatchChannelKind::Vector:
			if (Entry.PinType == EComposableCameraPinType::Rotator)
			{
				const FRotator R = GetBagDefault<FRotator>(Bag, Entry.Name);
				// Match the Pitch/Yaw/Roll->X/Y/Z mapping the runtime
				// SampleVectorCurves uses (FRotator(X, Y, Z)).
				PatchSection->AddVectorParameterKey(Entry.Name, CurrentFrame, FVector(R.Pitch, R.Yaw, R.Roll));
			}
			else
			{
				PatchSection->AddVectorParameterKey(Entry.Name, CurrentFrame, GetBagDefault<FVector>(Bag, Entry.Name));
			}
			break;
		case EPatchChannelKind::Color:
			PatchSection->AddColorParameterKey(Entry.Name, CurrentFrame, GetBagDefault<FLinearColor>(Bag, Entry.Name));
			break;
		default:
			break;
	}

	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
}

bool FComposableCameraPatchSectionInterface::CanPromoteParameter(FParameterMenuEntry Entry) const
{
	return WeakSequencer.IsValid() && !Entry.bAlreadyKeyed;
}

#undef LOCTEXT_NAMESPACE
