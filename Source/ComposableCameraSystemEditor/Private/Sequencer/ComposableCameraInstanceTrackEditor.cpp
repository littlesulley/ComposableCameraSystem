// Copyright 2026 Sulley. All Rights Reserved.
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Sequencer/ComposableCameraInstanceOutput.h"
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include "MVVM/Views/ViewUtilities.h"

#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "SequencerSectionPainter.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/SWindow.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "ComposableCameraInstanceTrackEditor"

namespace ComposableCameraSystem::CameraInstanceAuthoring
{
bool MakeCreationRange(FFrameRate TickResolution, FFrameNumber Start, double DurationSeconds,
	TRange<FFrameNumber>& Range, FString& Error)
{
	Error.Reset();
	Range = TRange<FFrameNumber>(Start, Start);
	if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0 || DurationSeconds > 3600.0)
	{ Error = TEXT("Choose a duration between zero and one hour."); return false; }
	if (TickResolution.Numerator <= 0 || TickResolution.Denominator <= 0)
	{ Error = TEXT("The sequence has an invalid tick resolution."); return false; }
	const int64 Length = FMath::Max<int64>(1, FMath::RoundToInt64(DurationSeconds * TickResolution.AsDecimal()));
	if (Length > MAX_int32 || static_cast<int64>(Start.Value) + Length > MAX_int32)
	{ Error = TEXT("The camera interval exceeds the sequence time range."); return false; }
	Range = TRange<FFrameNumber>(Start, Start + static_cast<int32>(Length));
	return true;
}

static bool HasCutConflict(const UMovieScene& Scene, const TRange<FFrameNumber>& Range, const UMovieSceneSection* Ignore = nullptr)
{
	const UMovieSceneTrack* Cuts = Scene.GetCameraCutTrack();
	if (!Cuts) return false;
	for (const UMovieSceneSection* Cut : Cuts->GetAllSections())
		if (Cut && Cut != Ignore && Cut->IsActive() && !TRange<FFrameNumber>::Intersection(Cut->GetRange(), Range).IsEmpty()) return true;
	return false;
}

bool CanReplaceCameraCut(const UMovieScene& Scene, const TRange<FFrameNumber>& Range, FString& Error)
{
	Error.Reset();
	if (Scene.IsReadOnly() || Range.IsEmpty() || !Range.HasLowerBound() || !Range.HasUpperBound())
	{ Error = TEXT("Choose a finite interval in an editable Level Sequence."); return false; }
	const auto* Cuts = Scene.GetCameraCutTrack();
	if ((!Cuts && !UMovieScene::IsTrackClassAllowed(UMovieSceneCameraCutTrack::StaticClass()))
		|| (Cuts && !Cuts->IsA<UMovieSceneCameraCutTrack>()))
	{ Error = TEXT("This sequence cannot create a Camera Cut Track."); return false; }
	if (Cuts) for (const auto* Cut : Cuts->GetAllSections())
		if (Cut && Cut->IsActive() && Cut->GetRange().Overlaps(Range) && Cut->IsLocked())
		{ Error = TEXT("A locked Camera Cut overlaps this interval. Unlock it or choose another interval."); return false; }
	return true;
}

static void ReleaseCutManagement(UMovieScene& Scene, const UMovieSceneSection& Cut)
{
	const auto Release = [&Cut](UMovieSceneTrack* Track)
	{
		if (auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track); Owner && Owner->ManagedCut == &Cut)
		{
			Owner->Modify();
			Owner->ManagedCut = nullptr;
		}
	};
	for (auto* Track : Scene.GetTracks()) Release(Track);
	for (const auto& Binding : Scene.GetBindings()) for (auto* Track : Binding.GetTracks()) Release(Track);
}

static bool ReplaceCameraCut(UMovieScene& Scene, UMovieSceneComposableCameraInstanceTrack& Owner,
	const TRange<FFrameNumber>& Range, FString& Error)
{
	if (!CanReplaceCameraCut(Scene, Range, Error)) return false;
	auto* Cuts = Cast<UMovieSceneCameraCutTrack>(Scene.GetCameraCutTrack());
	if (!Cuts) Cuts = Cast<UMovieSceneCameraCutTrack>(Scene.AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	if (!Cuts) { Error = TEXT("Could not create the Camera Cut Track."); return false; }
	Cuts->Modify();
	// RemoveSection normally fills gaps. Keep the exact outside ranges and the user's setting.
	const bool bAutoArrange = Cuts->IsAutoManagingSections();
	Cuts->SetIsAutoManagingSections(false);
	const auto Existing = Cuts->GetAllSections();
	for (auto* Base : Existing)
	{
		auto* Cut = Cast<UMovieSceneCameraCutSection>(Base);
		if (!Cut || !Cut->IsActive() || !Cut->GetRange().Overlaps(Range)) continue;
		const auto OldRange = Cut->GetRange();
		const auto Remainders = TRange<FFrameNumber>::Difference(OldRange, Range);
		// Otherwise a later Activation edit could expand the old Cut across the replacement.
		ReleaseCutManagement(Scene, *Cut);
		Cut->Modify();
		if (Remainders.IsEmpty()) { Cuts->RemoveSection(*Cut); continue; }
		auto* Right = Remainders.Num() == 2 ? DuplicateObject<UMovieSceneCameraCutSection>(Cut, Cuts) : nullptr;
		const auto SetRemainder = [&OldRange](UMovieSceneCameraCutSection& Section, const TRange<FFrameNumber>& Remainder)
		{
			Section.SetRange(Remainder);
			// Keep easing on original outer edges, not on newly cut internal boundaries.
			if (Remainder.GetLowerBound() != OldRange.GetLowerBound())
			{
				Section.Easing.AutoEaseInDuration = 0; Section.Easing.bManualEaseIn = false; Section.Easing.ManualEaseInDuration = 0;
			}
			if (Remainder.GetUpperBound() != OldRange.GetUpperBound())
			{
				Section.Easing.AutoEaseOutDuration = 0; Section.Easing.bManualEaseOut = false; Section.Easing.ManualEaseOutDuration = 0;
			}
		};
		SetRemainder(*Cut, Remainders[0]);
		if (Right) { SetRemainder(*Right, Remainders[1]); Cuts->AddSection(*Right); }
	}
	Owner.Modify();
	Owner.ManagedCut = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
	Owner.ManagedCut->SetRange(Range);
	Owner.ManagedCut->SetCameraBindingID(Owner.OutputBinding);
	Cuts->AddSection(*Owner.ManagedCut);
	// Keep native chronological ordering without invoking gap filling or blend arrangement.
	auto Sorted = Cuts->GetAllSections();
	MovieSceneHelpers::SortConsecutiveSections(Sorted);
	Cuts->RemoveAllAnimationData();
	for (auto* Cut : Sorted) Cuts->AddSection(*Cut);
	Cuts->SetIsAutoManagingSections(bAutoArrange);
	return true;
}

bool CanSetCameraCut(TSharedRef<ISequencer> Sequencer, const UMovieSceneComposableCameraInstanceTrack& Owner,
	const UMovieSceneSection& Activation, FString& Error)
{
	Error.Reset();
	const auto* Sequence = Cast<ULevelSequence>(Sequencer->GetFocusedMovieSceneSequence());
	const auto* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!GEditor || GIsTransacting || GEditor->IsTransactionActive() || !Scene || Sequencer->IsReadOnly()
		|| Owner.GetOuter() != Scene || !Owner.IsInMovieScene() || !Owner.Instance || !Owner.HasSection(Activation)
		|| Activation.IsLocked() || !Activation.IsActive())
	{ Error = TEXT("Focus this clip's editable Level Sequence and choose an enabled, unlocked Activation outside an edit gesture."); return false; }
	if (!CanReplaceCameraCut(*Scene, Activation.GetRange(), Error)) return false;
	if (!CameraInstanceOutput::HasOwnedOutput(Sequencer, Owner))
	{ Error = TEXT("Repair Managed Output from the Activation track's context menu, then set its Camera Cut."); return false; }
	return true;
}

bool SetCameraCut(TSharedRef<ISequencer> Sequencer, UMovieSceneComposableCameraInstanceTrack& Owner,
	UMovieSceneSection& Activation, FString& Error)
{
	if (!CanSetCameraCut(Sequencer, Owner, Activation, Error)) return false;
	auto* Scene = Owner.GetTypedOuter<UMovieScene>();
	auto* Cuts = Scene->GetCameraCutTrack();
	const auto* Existing = Owner.ManagedCut.Get();
	if (Cuts && Existing && Cuts->HasSection(*Existing) && Existing->IsActive()
		&& Existing->GetRange() == Activation.GetRange() && Existing->GetCameraBindingID() == Owner.OutputBinding
		&& !HasCutConflict(*Scene, Activation.GetRange(), Existing)) return true;
	{
		FScopedTransaction Transaction(LOCTEXT("SetCameraCut", "Set Camera Instance Cut"));
		Sequencer->GetFocusedMovieSceneSequence()->Modify(); Scene->Modify();
		if (!ReplaceCameraCut(*Scene, Owner, Activation.GetRange(), Error)) { Transaction.Cancel(); return false; }
	}
	// Notify after committing: automatic output repair must not enlarge this Cut-only transaction.
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	return true;
}

UMovieSceneComposableCameraInstanceTrack* Create(TSharedRef<ISequencer> Sequencer,
	UObject* Source, FFrameNumber Start, FString& Error, double DurationSeconds, FText Label)
{
	Error.Reset();
	auto* Sequence = Cast<ULevelSequence>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	UWorld* World = Cast<UWorld>(Sequencer->GetPlaybackContext());
	const auto* Preset = Cast<UComposableCameraShotAsset>(Source);
	auto* Type = Cast<UComposableCameraTypeAsset>(Source);
	const auto* Draft = Cast<UComposableCameraInstanceData>(Source);
	if (!Scene || Scene->IsReadOnly() || Sequencer->IsReadOnly() || !World || (!Preset && !Type && !Draft) || !Sequence->AllowsSpawnableObjects())
	{ Error = TEXT("Open an editable Level Sequence with a valid world and Spawnable support."); return nullptr; }
	if (Type && Type->IsA<UComposableCameraPatchTypeAsset>()) { Error = TEXT("Camera Patches use the existing Patch Track workflow."); return nullptr; }
	if (Draft && (!Draft->IsTypeInterfaceCurrent() || !Draft->IsTypeInterfaceValid()))
	{ Error = TEXT("The Camera Type interface changed. Refresh the draft before inserting it."); return nullptr; }
	TRange<FFrameNumber> Range;
	if (!MakeCreationRange(Scene->GetTickResolution(), Start, DurationSeconds, Range, Error)) return nullptr;
	if (!CanReplaceCameraCut(*Scene, Range, Error)) return nullptr;
	FScopedTransaction Transaction(LOCTEXT("Create", "Create Camera Instance"));
	Sequence->Modify();
	Scene->Modify();
	auto* Track = Scene->AddTrack<UMovieSceneComposableCameraInstanceTrack>();
	if (!Track) { Error = TEXT("Could not create Camera Instance Track."); return nullptr; }
	Track->SetDisplayName(Label.IsEmpty() ? FText::FromString(Source->GetName()) : Label);
	if (Preset) Track->Instance->InitializeFromShot(Preset->Shot, const_cast<UComposableCameraShotAsset*>(Preset));
	else if (Type) Track->Instance->InitializeFromType(Type);
	else Track->Instance->CopyForAuthoring(*Draft);
	auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Track->CreateNewSection());
	Section->SetRange(Range);
	Track->AddSection(*Section);
	if (!CameraInstanceOutput::Ensure(Sequencer, *Track, Error))
	{
		Scene->RemoveTrack(*Track);
		return nullptr;
	}
	if (!ReplaceCameraCut(*Scene, *Track, Range, Error))
	{
		const FGuid Binding = Track->OutputBinding.GetGuid();
		Sequence->UnbindPossessableObjects(Binding);
		Scene->RemoveSpawnable(Binding); Scene->RemovePossessable(Binding); Scene->RemoveTrack(*Track);
		Transaction.Cancel();
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
		return nullptr;
	}
	if (!Scene->IsPlaybackRangeLocked()) Scene->SetPlaybackRange(TRange<FFrameNumber>::Hull(Scene->GetPlaybackRange(), Range));
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	Sequencer->EmptySelection();
	Sequencer->SelectSection(Section);
	Sequencer->SetPerspectiveViewportCameraCutEnabled(true);
	return Track;
}

UMovieSceneComposableCameraInstanceTrack* DuplicateAtTime(TSharedRef<ISequencer> Sequencer,
	UMovieSceneComposableCameraInstanceTrack& Source, UMovieSceneSection& Anchor, FFrameNumber Start, FString& Error)
{
	Error.Reset();
	auto* Sequence = Cast<ULevelSequence>(Sequencer->GetFocusedMovieSceneSequence());
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Scene || Scene->IsReadOnly() || Sequencer->IsReadOnly() || !Cast<UWorld>(Sequencer->GetPlaybackContext())
		|| !Sequence->AllowsSpawnableObjects() || Source.GetOuter() != Scene || !Source.IsInMovieScene() || !Source.Instance
		|| !Source.HasSection(Anchor) || !Anchor.HasStartFrame() || !Anchor.HasEndFrame() || Anchor.IsLocked()
		|| Anchor.GetRange().IsEmpty())
	{ Error = TEXT("Open the clip's editable Level Sequence; its activation must have a finite range and be unlocked."); return nullptr; }
	if (!GEditor || GEditor->IsTransactionActive())
	{ Error = TEXT("Finish the current edit gesture before duplicating a camera instance."); return nullptr; }
	const int64 Delta64 = static_cast<int64>(Start.Value) - Anchor.GetInclusiveStartFrame().Value;
	if (Delta64 < MIN_int32 || Delta64 > MAX_int32)
	{ Error = TEXT("The duplicate exceeds sequence frame-number limits."); return nullptr; }
	// Capture sibling lanes before output creation mutates the MovieScene binding array.
	TArray<UMovieSceneTrack*> SourceTracks{&Source};
	if (const FMovieSceneBinding* Binding = Scene->FindBinding(Source.FindObjectBindingGuid()))
		for (UMovieSceneTrack* Track : Binding->GetTracks())
			if (Track->IsA<UMovieSceneComposableCameraInstanceParameterTrack>()) SourceTracks.Add(Track);
	const auto CanShift = [Delta64](FFrameNumber Time)
	{
		const int64 Shifted = static_cast<int64>(Time.Value) + Delta64;
		return Shifted >= MIN_int32 && Shifted <= MAX_int32;
	};
	const auto CanShiftKeys = [&CanShift](const auto& Channel)
	{
		for (FFrameNumber Time : Channel.GetData().GetTimes()) if (!CanShift(Time)) return false;
		return true;
	};
	for (const UMovieSceneTrack* Track : SourceTracks)
	{
		if (!UMovieScene::IsTrackClassAllowed(Track->GetClass()))
		{ Error = TEXT("This sequence does not allow a required camera track class."); return nullptr; }
		for (const UMovieSceneSection* Base : Track->GetAllSections())
		{
			const auto* Section = Cast<UMovieSceneComposableCameraInstanceSection>(Base);
			if (!Section) { Error = TEXT("The instance contains an unsupported section type."); return nullptr; }
			const auto Range = Section->GetRange();
			if ((Range.HasLowerBound() && !CanShift(Range.GetLowerBoundValue()))
				|| (Range.HasUpperBound() && !CanShift(Range.GetUpperBoundValue())))
			{ Error = TEXT("A copied section would exceed sequence frame-number limits."); return nullptr; }
			for (const auto& Channel : Section->Channels)
				if (!CanShiftKeys(Channel.Number) || !CanShiftKeys(Channel.Integer) || !CanShiftKeys(Channel.Boolean))
				{ Error = TEXT("A copied keyframe would exceed sequence frame-number limits."); return nullptr; }
		}
	}
	const FFrameNumber Delta(static_cast<int32>(Delta64));
	const int32 AnchorIndex = Source.GetAllSections().IndexOfByKey(&Anchor);
	auto CutRange = Anchor.GetRange();
	CutRange.SetLowerBoundValue(CutRange.GetLowerBoundValue() + Delta);
	CutRange.SetUpperBoundValue(CutRange.GetUpperBoundValue() + Delta);
	if (!CanReplaceCameraCut(*Scene, CutRange, Error)) return nullptr;
	const auto ShiftSections = [Delta](UMovieSceneTrack& Track)
	{
		for (UMovieSceneSection* Base : Track.GetAllSections())
		{
			auto* Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Base);
			Section->RebuildChannelProxy();
			const bool bLocked = Section->IsLocked();
			Section->SetIsLocked(false);
			// Native movement offsets both finite bounds and channel keys, retaining open bounds.
			Section->MoveSection(Delta);
			Section->SetIsLocked(bLocked);
		}
	};
	UMovieSceneComposableCameraInstanceTrack* Copy = nullptr;
	UMovieSceneSection* CopiedAnchor = nullptr;
	{
		FScopedTransaction Transaction(LOCTEXT("DuplicateInstance", "Duplicate Camera Instance At Playhead"));
		Sequence->Modify(); Scene->Modify();
		Copy = DuplicateObject<UMovieSceneComposableCameraInstanceTrack>(&Source, Scene);
		Copy->OutputBinding = FMovieSceneObjectBindingID();
		Copy->ManagedCut = nullptr;
		const FGuid SourceBinding = Source.FindObjectBindingGuid();
		const FText Name = SourceBinding.IsValid() ? Scene->GetObjectDisplayName(SourceBinding) : Source.GetDisplayName();
		Copy->SetDisplayName(FText::Format(LOCTEXT("DuplicateName", "{0} Copy"), Name));
		ShiftSections(*Copy);
		if (!Scene->AddGivenTrack(Copy) || !CameraInstanceOutput::Ensure(Sequencer, *Copy, Error))
		{
			Scene->RemoveTrack(*Copy);
			Transaction.Cancel();
			if (Error.IsEmpty()) Error = TEXT("Could not create the duplicate's output camera.");
			return nullptr;
		}
		const FGuid Output = Copy->FindObjectBindingGuid();
		for (int32 Index = 1; Index < SourceTracks.Num(); ++Index)
		{
			auto* Parameter = DuplicateObject<UMovieSceneComposableCameraInstanceParameterTrack>(
				CastChecked<UMovieSceneComposableCameraInstanceParameterTrack>(SourceTracks[Index]), Scene);
			ShiftSections(*Parameter);
			if (!Scene->AddGivenTrack(Parameter, Output))
			{
				// Roll back only this creation, never an unrelated previous Undo transaction.
				Sequence->UnbindPossessableObjects(Output);
				Scene->RemoveSpawnable(Output); Scene->RemovePossessable(Output);
				Transaction.Cancel();
				Error = TEXT("Could not attach a copied parameter track.");
				return nullptr;
			}
		}
		CopiedAnchor = Copy->GetAllSections()[AnchorIndex];
		const auto Range = CopiedAnchor->GetRange();
		if (!ReplaceCameraCut(*Scene, *Copy, Range, Error))
		{
			Sequence->UnbindPossessableObjects(Output);
			Scene->RemoveSpawnable(Output); Scene->RemovePossessable(Output); Scene->RemoveTrack(*Copy);
			Transaction.Cancel();
			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
			return nullptr;
		}
		if (!Scene->IsPlaybackRangeLocked())
			for (const UMovieSceneSection* Section : Copy->GetAllSections())
				if (Section->HasStartFrame() && Section->HasEndFrame())
					Scene->SetPlaybackRange(TRange<FFrameNumber>::Hull(Scene->GetPlaybackRange(), Section->GetRange()));
	}
	// Refresh models immediately before selecting the new section.
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	Sequencer->EmptySelection();
	Sequencer->SelectSection(CopiedAnchor);
	Sequencer->ThrobSectionSelection();
	return Copy;
}

bool SetSubjectBinding(UMovieSceneComposableCameraInstanceTrack& Track, FName Input, FGuid LocalBinding)
{
	UMovieSceneSequence* Sequence = Track.GetTypedOuter<UMovieSceneSequence>();
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Track.Instance || !Scene || Scene->IsReadOnly() || !Track.IsInMovieScene()) return false;
	TStrongObjectPtr<UComposableCameraInstanceData> Candidate(NewObject<UComposableCameraInstanceData>());
	Candidate->CopyConfiguration(*Track.Instance);
	if (!Candidate->RefreshTypeInterface()) return false;
	TArray<FComposableCameraInstanceField> Fields;
	Candidate->GatherFields(Fields);
	const auto* Field = Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Input; });
	const auto* Property = Field ? CastField<FObjectPropertyBase>(Field->Property) : nullptr;
	if (!Property || !Property->PropertyClass->IsChildOf(AActor::StaticClass())) return false;
	const UClass* Class = LocalBinding.IsValid() ? MovieSceneHelpers::GetBoundObjectClass(Sequence, LocalBinding) : nullptr;
	if (LocalBinding.IsValid() && (!Class || !Class->IsChildOf(Property->PropertyClass))) return false;
	Track.Modify();
	Track.Instance->Modify();
	auto* Slot = Candidate->Subjects.FindByPredicate([&](const auto& Item) { return Item.Input == Input; });
	if (!Slot) return false;
	Slot->Binding = LocalBinding.IsValid() ? FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(LocalBinding)) : FMovieSceneObjectBindingID();
	Field->WriteObject(nullptr);
	Track.Instance->CopyConfiguration(*Candidate.Get());
	Track.MarkAsChanged();
	return true;
}
}

namespace ComposableCameraSystem::CameraInstanceAuthoring
{
void BuildSubjectsMenu(FMenuBuilder& Menu, UMovieSceneComposableCameraInstanceTrack* Owner, TSharedPtr<ISequencer> Sequencer)
{
	TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack> Instance = Owner;
	TWeakPtr<ISequencer> WeakSequencer = Sequencer;
	Menu.AddSubMenu(LOCTEXT("Subjects", "Bind Subjects"), LOCTEXT("SubjectsTip", "Choose Sequence bindings, including actors that are not currently spawned."),
		FNewMenuDelegate::CreateLambda([Instance, WeakSequencer](FMenuBuilder& InputsMenu)
		{
			if (!Instance.IsValid() || !Instance->Instance) return;
			TStrongObjectPtr<UComposableCameraInstanceData> Candidate(NewObject<UComposableCameraInstanceData>());
			Candidate->CopyConfiguration(*Instance->Instance);
			if (!Candidate->RefreshTypeInterface()) return;
			TArray<FComposableCameraInstanceField> Fields;
			Candidate->GatherFields(Fields);
			for (const auto& Field : Fields)
			{
				const auto* Property = CastField<FObjectPropertyBase>(Field.Property);
				if (!Property || !Property->PropertyClass->IsChildOf(AActor::StaticClass())) continue;
				const FName Path = Field.Path;
				InputsMenu.AddSubMenu(ComposableCameraSystem::CameraInstanceAuthoring::ParameterLabel(*Instance->Instance, Path), FText(), FNewMenuDelegate::CreateLambda([Instance, WeakSequencer, Path](FMenuBuilder& BindingsMenu)
				{
					auto* Sequence = Instance.IsValid() ? Instance->GetTypedOuter<UMovieSceneSequence>() : nullptr;
					UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
					if (!Scene) return;
					auto AddBinding = [&](FGuid Guid, FText Label)
					{
						BindingsMenu.AddMenuEntry(Label, FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Instance, WeakSequencer, Path, Guid]()
						{
							const auto Sequencer = WeakSequencer.Pin();
							if (!Instance.IsValid() || !Sequencer || Sequencer->IsReadOnly()
								|| Sequencer->GetFocusedMovieSceneSequence() != Instance->GetTypedOuter<UMovieSceneSequence>()) return;
							const FScopedTransaction Transaction(LOCTEXT("BindSubject", "Bind Camera Instance Subject"));
							if (ComposableCameraSystem::CameraInstanceAuthoring::SetSubjectBinding(*Instance.Get(), Path, Guid))
								Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
						})));
					};
					AddBinding(FGuid(), LOCTEXT("ClearSubject", "Clear"));
					for (const auto& Binding : Scene->GetBindings())
					{
						const UClass* Class = MovieSceneHelpers::GetBoundObjectClass(Sequence, Binding.GetObjectGuid());
						if (Class && Class->IsChildOf(AActor::StaticClass())) AddBinding(Binding.GetObjectGuid(), Scene->GetObjectDisplayName(Binding.GetObjectGuid()));
					}
				}));
			}
		}));

}
}

namespace
{
class FCameraInstanceSectionInterface : public ISequencerSection
{
public:
	FCameraInstanceSectionInterface(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer) : Section(InSection), Sequencer(InSequencer) {}
	virtual UMovieSceneSection* GetSectionObject() override { return &Section; }
	virtual FText GetSectionTitle() const override
	{
		return Section.GetTypedOuter<UMovieSceneComposableCameraInstanceParameterTrack>() ? FText::GetEmpty() : LOCTEXT("Interval", "Camera Instance");
	}
	virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override { return Painter.PaintSectionBackground(); }
	virtual FReply OnSectionDoubleClicked(const FGeometry&, const FPointerEvent&) override
	{
		FComposableCameraShotEditor::OpenForCameraInstance(UMovieSceneComposableCameraInstanceTrack::FindForSection(Section));
		return FReply::Handled();
	}
	virtual FReply OnSectionDoubleClicked(const FGeometry& Geometry, const FPointerEvent& Event, const FGuid&) override
	{ return OnSectionDoubleClicked(Geometry, Event); }
	virtual void BuildSectionContextMenu(FMenuBuilder& Menu, const FGuid&) override
	{
		TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack> Track = UMovieSceneComposableCameraInstanceTrack::FindForSection(Section);
		Menu.AddMenuEntry(LOCTEXT("OpenShot", "Open in Shot Editor"), FText(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Track]() { FComposableCameraShotEditor::OpenForCameraInstance(Track.Get()); })));
		if (auto* ParameterSection = Cast<UMovieSceneComposableCameraInstanceSection>(&Section);
			ParameterSection && ParameterSection->GetTypedOuter<UMovieSceneComposableCameraInstanceParameterTrack>())
		{
			TWeakObjectPtr<UMovieSceneComposableCameraInstanceSection> WeakSection = ParameterSection;
			TWeakPtr<ISequencer> WeakSequencer = Sequencer;
			Menu.AddMenuEntry(LOCTEXT("InfiniteParameter", "Make Section Infinite"),
				LOCTEXT("InfiniteParameterTip", "Remove this parameter section's time bounds, preserving all keys. Activation still controls the camera's active range."), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakSection, WeakSequencer]()
				{
					if (const auto Seq = WeakSequencer.Pin(); Seq && WeakSection.IsValid())
						ComposableCameraSystem::CameraInstanceAuthoring::MakeParameterSectionInfinite(*WeakSection.Get(), *Seq);
				})));
		}
	}

private:
	UMovieSceneSection& Section;
	TWeakPtr<ISequencer> Sequencer;
};
}

TSharedRef<ISequencerTrackEditor> FComposableCameraInstanceTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> Sequencer)
{
	return MakeShared<FComposableCameraInstanceTrackEditor>(Sequencer);
}
FComposableCameraInstanceTrackEditor::FComposableCameraInstanceTrackEditor(TSharedRef<ISequencer> Sequencer) : FMovieSceneTrackEditor(Sequencer)
{
	DataChangedHandle = Sequencer->OnMovieSceneDataChanged().AddRaw(this, &FComposableCameraInstanceTrackEditor::OnDataChanged);
	SequenceActivatedHandle = Sequencer->OnActivateSequence().AddLambda([this](FMovieSceneSequenceIDRef) { CacheManagedCuts(); });
	CacheManagedCuts();
}
FComposableCameraInstanceTrackEditor::~FComposableCameraInstanceTrackEditor()
{
	if (const auto Sequencer = GetSequencer())
	{
		Sequencer->OnMovieSceneDataChanged().Remove(DataChangedHandle);
		Sequencer->OnActivateSequence().Remove(SequenceActivatedHandle);
	}
}
bool FComposableCameraInstanceTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const { return Type == UMovieSceneComposableCameraInstanceTrack::StaticClass() || Type == UMovieSceneComposableCameraInstanceParameterTrack::StaticClass(); }
bool FComposableCameraInstanceTrackEditor::SupportsSequence(UMovieSceneSequence* Sequence) const { return Sequence && Sequence->IsA<ULevelSequence>(); }
bool FComposableCameraInstanceTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid&)
{
	// Empty Sequencer space bubbles to its native asset-drop path. Only this live
	// operation can transport a transient instance snapshot; it is not a new asset type.
	if (Asset && Asset->IsA<UComposableCameraInstanceData>())
	{
		const auto Draft = FComposableCameraInstanceDragDropOp::FindForSnapshot(Asset);
		if (!Draft) return false;
		if (const auto Sequencer = GetSequencer()) Draft->Drop(Sequencer.ToSharedRef(), Draft->DropFrame(*Sequencer));
		return true;
	}
	// Sequencer calls every registered editor, even after one consumes the drop.
	// This is the sole ShotAsset/CameraTypeAsset handler, including drops targeting
	// legacy LSActor bindings. Patch subclasses retain their own exclusive handler.
	if (!Asset || (!Asset->IsA<UComposableCameraShotAsset>() && !Asset->IsA<UComposableCameraTypeAsset>())) return false;
	if (Asset->IsA<UComposableCameraPatchTypeAsset>()) return false;
	const auto Sequencer = GetSequencer();
	if (!Sequencer) return false;
	FString Error;
	ComposableCameraSystem::CameraInstanceAuthoring::Create(Sequencer.ToSharedRef(), Asset, Sequencer->GetLocalTime().Time.FloorToFrame(), Error);
	if (!Error.IsEmpty())
	{
		FNotificationInfo Info(FText::FromString(Error));
		Info.ExpireDuration = 8.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return true;
}

bool FComposableCameraInstanceTrackEditor::OnAllowDrop(const FDragDropEvent& Event, FSequencerDragDropParams& Params)
{
	const auto Op = Event.GetOperationAs<FComposableCameraInstanceDragDropOp>();
	const auto Sequencer = GetSequencer();
	if (!Op || !Sequencer) return false;
	// Sequencer paints both range endpoints after any true OnAllowDrop result.
	// Its incoming default range is unbounded, including for rejected own drops.
	Op->UpdateFeedback(*Sequencer, Params.FrameNumber, Params.FrameRange);
	// Consume this operation even when rejected, so it cannot fall through to an actor drop.
	return true;
}

FReply FComposableCameraInstanceTrackEditor::OnDrop(const FDragDropEvent& Event, const FSequencerDragDropParams& Params)
{
	const auto Op = Event.GetOperationAs<FComposableCameraInstanceDragDropOp>();
	const auto Sequencer = GetSequencer();
	if (!Op || !Sequencer) return FReply::Unhandled();
	Op->Drop(Sequencer.ToSharedRef(), Params.FrameNumber);
	return FReply::Handled();
}

void FComposableCameraInstanceTrackEditor::BuildAddTrackMenu(FMenuBuilder& Menu)
{
	Menu.AddMenuEntry(LOCTEXT("NewDraft", "New Camera Shot..."),
		LOCTEXT("NewDraftTip", "Compose a draft against this sequence, then drag it into the timeline."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Weak = TWeakPtr<ISequencer>(GetSequencer())]()
		{
			if (const auto Seq = Weak.Pin()) FComposableCameraShotEditor::OpenForSequenceDraft(Seq.ToSharedRef());
		})));
}
TSharedRef<ISequencerSection> FComposableCameraInstanceTrackEditor::MakeSectionInterface(UMovieSceneSection& Section, UMovieSceneTrack&, FGuid)
{
	return MakeShared<FCameraInstanceSectionInterface>(Section, GetSequencer());
}
void FComposableCameraInstanceTrackEditor::BuildTrackContextMenu(FMenuBuilder& Menu, UMovieSceneTrack* Track)
{
	auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track);
	if (!Owner && Track) Owner = UMovieSceneComposableCameraInstanceTrack::Find(Track->GetTypedOuter<UMovieScene>(), Track->FindObjectBindingGuid());
	TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack> Instance = Owner;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencer();
	Menu.AddMenuEntry(LOCTEXT("Edit", "Open in Shot Editor"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Instance, WeakSequencer]() { FComposableCameraShotEditor::OpenForCameraInstance(Instance.Get()); })));
	ComposableCameraSystem::CameraInstanceAuthoring::BuildSubjectsMenu(Menu, Owner, GetSequencer());
	Menu.AddMenuEntry(LOCTEXT("RepairOutput", "Repair Managed Output"), LOCTEXT("RepairOutputTip", "Recreate a deleted output or give a pasted instance its own camera."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Instance, WeakSequencer]()
		{
			const auto Sequencer = WeakSequencer.Pin();
			if (!Instance.IsValid() || !Sequencer || Sequencer->IsReadOnly()) return;
			FString Error;
			const FScopedTransaction Transaction(LOCTEXT("Repair", "Repair Camera Instance Output"));
			if (ComposableCameraSystem::CameraInstanceOutput::Ensure(Sequencer.ToSharedRef(), *Instance.Get(), Error))
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
			else if (!Error.IsEmpty())
			{
				FNotificationInfo Info(FText::FromString(Error));
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		})));
}
void FComposableCameraInstanceTrackEditor::CacheManagedCuts()
{
	const auto Sequencer = GetSequencer();
	auto* Scene = Sequencer && Sequencer->GetFocusedMovieSceneSequence() ? Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene() : nullptr;
	CachedScene = Scene;
	KnownInstances.Reset();
	ManagedCutStates.Reset();
	if (!Scene) return;
	const auto CacheTrack = [this](UMovieSceneTrack* Track)
	{
		auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track);
		if (!Owner) return;
		KnownInstances.Add(Owner);
		if (!Owner->ManagedCut) return;
		FManagedCutState& State = ManagedCutStates.Add(Owner);
		State.Cut = Owner->ManagedCut.Get();
		State.CutRange = Owner->ManagedCut->GetRange();
		if (Owner->GetAllSections().Num() == 1)
			if (auto* Activation = Owner->GetAllSections()[0])
			{
				State.Activation = Activation;
				State.ActivationRange = Activation->GetRange();
			}
	};
	for (UMovieSceneTrack* Track : Scene->GetTracks()) CacheTrack(Track);
	for (const auto& Binding : Scene->GetBindings())
		for (UMovieSceneTrack* Track : Binding.GetTracks()) CacheTrack(Track);
}

void FComposableCameraInstanceTrackEditor::OnDataChanged(EMovieSceneDataChangeType)
{
	// Never create a second Undo entry from a notification, or mutate while Undo/Redo is
	// restoring objects. Explicit repair remains available for external, non-transactional edits.
	if (bSynchronizing || ComposableCameraSystem::CameraInstanceOutput::IsCreating()) return;
	const auto Sequencer = GetSequencer();
	UMovieScene* Scene = Sequencer && Sequencer->GetFocusedMovieSceneSequence() ? Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene() : nullptr;
	if (!Scene || CachedScene.Get() != Scene || GIsTransacting || !GEditor || !GEditor->IsTransactionActive()
		|| Scene->IsReadOnly() || Sequencer->IsReadOnly())
	{
		// Set Camera Cut notifies after commit; Undo/Redo and focus changes also need
		// a fresh baseline without rewriting restored/authored data.
		CacheManagedCuts();
		return;
	}
	TGuardValue<bool> Guard(bSynchronizing, true);
	bool bChanged = false;
	// Native deletion of the top binding also removes its tracks. Retain weak identities
	// across the notification so the exact generated Cut can join the same Undo transaction.
	for (const auto WeakOwner : KnownInstances)
		if (const auto* Owner = WeakOwner.Get(); Owner && Owner->GetOuter() == Scene && !Owner->IsInMovieScene())
			if (auto* Cuts = Scene->GetCameraCutTrack(); Cuts && Owner->ManagedCut && Cuts->HasSection(*Owner->ManagedCut)
				&& !Owner->ManagedCut->IsLocked() && Owner->ManagedCut->GetCameraBindingID() == Owner->OutputBinding)
			{ Cuts->Modify(); Cuts->RemoveSection(*Owner->ManagedCut); bChanged = true; }
	bChanged |= ComposableCameraSystem::CameraInstanceOutput::RemoveOrphans(Sequencer.ToSharedRef());
	TArray<UMovieSceneTrack*> InstanceTracks = Scene->GetTracks();
	for (const auto& Binding : Scene->GetBindings()) InstanceTracks.Append(Binding.GetTracks());
	TMap<UMovieSceneSection*, TRange<FFrameNumber>> PendingCutRanges;
	for (UMovieSceneTrack* Root : InstanceTracks)
	{
		auto* Track = Cast<UMovieSceneComposableCameraInstanceTrack>(Root);
		if (!Track) continue;
		const FMovieSceneObjectBindingID PreviousOutput = Track->OutputBinding;
		const FGuid PreviousParent = Track->FindObjectBindingGuid();
		FString Error;
		if (!ComposableCameraSystem::CameraInstanceOutput::Ensure(Sequencer.ToSharedRef(), *Track, Error)) continue;
		bChanged |= Track->OutputBinding != PreviousOutput || Track->FindObjectBindingGuid() != PreviousParent;
		if (!Track->ManagedCut || Track->ManagedCut->IsLocked() || Track->ManagedCut->GetCameraBindingID() != Track->OutputBinding) continue;
		const UMovieSceneTrack* Cuts = Scene->GetCameraCutTrack();
		if (!Cuts || !Cuts->HasSection(*Track->ManagedCut)) continue;
		const FManagedCutState* Previous = ManagedCutStates.Find(Track);
		if (!Previous || Previous->Cut.Get() != Track->ManagedCut.Get()) continue;
		const UMovieSceneSection* Section = Track->GetAllSections().Num() == 1 ? Track->GetAllSections()[0] : nullptr;
		if (Section && Track->ManagedCut->GetRange() == Section->GetRange()) continue;
		if (Track->ManagedCut->GetRange() != Previous->CutRange)
		{
			// Native Cut moves/trims (including adjacent Cut auto-arrange) take ownership
			// of the edit. Keep the camera binding, but stop forcing its Activation range.
			Track->Modify();
			Track->ManagedCut = nullptr;
			bChanged = true;
		}
		else if (Section && Previous->Activation.Get() == Section && Section->GetRange() != Previous->ActivationRange)
		{
			PendingCutRanges.Add(Track->ManagedCut.Get(), Section->GetRange());
		}
	}
	// Compare the whole edit's destinations before moving any Cut. If a move is
	// blocked, its old range must also block any other move that depended on it leaving.
	if (const UMovieSceneTrack* Cuts = Scene->GetCameraCutTrack())
	{
		bool bRejected;
		do
		{
			bRejected = false;
			for (auto Move = PendingCutRanges.CreateIterator(); Move; ++Move)
				for (UMovieSceneSection* Other : Cuts->GetAllSections())
				{
					if (!Other || Other == Move.Key() || !Other->IsActive()) continue;
					const auto* Destination = PendingCutRanges.Find(Other);
					if (Move.Value().Overlaps(Destination ? *Destination : Other->GetRange()))
					{
						Move.RemoveCurrent(); bRejected = true; break;
					}
				}
		} while (bRejected);
	}
	for (const auto& Move : PendingCutRanges)
	{
		Move.Key->Modify();
		Move.Key->SetRange(Move.Value);
		bChanged = true;
	}
	CacheManagedCuts();
	if (bChanged) Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FComposableCameraInstanceTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& Menu, const TArray<FGuid>& Bindings, const UClass*)
{
	const auto Seq = GetSequencer();
	if (!Seq || Bindings.Num() != 1 || !Seq->GetFocusedMovieSceneSequence()) return;
	auto* Owner = UMovieSceneComposableCameraInstanceTrack::Find(Seq->GetFocusedMovieSceneSequence()->GetMovieScene(), Bindings[0]);
	if (!Owner || !Owner->Instance) return;
	TWeakObjectPtr<UMovieSceneComposableCameraInstanceTrack> WeakOwner = Owner;
	TWeakPtr<ISequencer> WeakSeq = Seq;
	Menu.AddSubMenu(LOCTEXT("Inputs", "Camera Parameters"), FText(), FNewMenuDelegate::CreateLambda([WeakOwner, WeakSeq](FMenuBuilder& Inputs)
	{
		if (!WeakOwner.IsValid() || !WeakOwner->Instance) return;
		TStrongObjectPtr<UComposableCameraInstanceData> Candidate(NewObject<UComposableCameraInstanceData>());
		Candidate->CopyConfiguration(*WeakOwner->Instance);
		if (!Candidate->RefreshTypeInterface()) return;
		TArray<FComposableCameraInstanceField> Fields;
		Candidate->GatherFields(Fields);
		for (const auto& Field : Fields)
		{
			if (Field.Type == EComposableCameraInstanceFieldType::Object) continue;
			const FName Path = Field.Path;
			Inputs.AddMenuEntry(ComposableCameraSystem::CameraInstanceAuthoring::ParameterLabel(*WeakOwner->Instance, Path), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakOwner, WeakSeq, Path]()
			{
				const auto Seq = WeakSeq.Pin();
				if (!WeakOwner.IsValid() || !Seq || Seq->IsReadOnly() || Seq->GetFocusedMovieSceneSequence() != WeakOwner->GetTypedOuter<UMovieSceneSequence>()) return;
				const FScopedTransaction Transaction(LOCTEXT("AddParameter", "Add Camera Parameter Track"));
				if (ComposableCameraSystem::CameraInstanceAuthoring::AddParameterTrack(*WeakOwner.Get(), Path, Seq->GetLocalTime().Time))
					Seq->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
			}), FCanExecuteAction::CreateLambda([WeakOwner, WeakSeq, Path]()
			{
				const auto Seq = WeakSeq.Pin();
				return WeakOwner.IsValid() && Seq && !Seq->IsReadOnly()
					&& !ComposableCameraSystem::CameraInstanceAuthoring::FindParameterTrack(*WeakOwner.Get(), Path);
			})));
		}
	}));
}
void FComposableCameraInstanceTrackEditor::BuildObjectBindingContextMenu(FMenuBuilder& Menu, const TArray<FGuid>& Bindings, const UClass*)
{
	const auto Seq = GetSequencer();
	if (Seq && Bindings.Num() == 1 && Seq->GetFocusedMovieSceneSequence())
		if (auto* Owner = UMovieSceneComposableCameraInstanceTrack::Find(Seq->GetFocusedMovieSceneSequence()->GetMovieScene(), Bindings[0]))
			BuildTrackContextMenu(Menu, Owner);
}
TSharedPtr<SWidget> FComposableCameraInstanceTrackEditor::BuildOutlinerEditWidget(const FGuid&, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	TWeakObjectPtr<UMovieSceneTrack> WeakTrack = Track;
	TWeakPtr<ISequencer> WeakSeq = GetSequencer();
	return UE::Sequencer::MakeAddButton(LOCTEXT("Section", "Section"), FOnGetContent::CreateLambda([WeakTrack, WeakSeq]()
	{
		FMenuBuilder Menu(true, nullptr);
		Menu.AddMenuEntry(LOCTEXT("AddSection", "Add Section"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakTrack, WeakSeq]()
		{
			const auto Seq = WeakSeq.Pin();
			if (!WeakTrack.IsValid() || !Seq || Seq->IsReadOnly() || Seq->GetFocusedMovieSceneSequence() != WeakTrack->GetTypedOuter<UMovieSceneSequence>()) return;
			const FScopedTransaction Transaction(LOCTEXT("NewSection", "Add Camera Section"));
			if (auto* Section = ComposableCameraSystem::CameraInstanceAuthoring::AddInterval(*WeakTrack.Get(), Seq->GetLocalTime().Time))
			{
				Seq->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
				Seq->EmptySelection(); Seq->SelectSection(Section);
			}
		})));
		return Menu.MakeWidget();
	}), Params.ViewModel);
}
void FComposableCameraInstanceTrackEditor::InitializeBindingNames()
{
	const auto Seq = GetSequencer();
	auto* Scene = Seq && Seq->GetFocusedMovieSceneSequence() ? Seq->GetFocusedMovieSceneSequence()->GetMovieScene() : nullptr;
	if (!Scene || NamingScene.Get() == Scene || Scene->IsReadOnly() || Seq->IsReadOnly() || GIsTransacting) return;
	NamingScene = Scene;
	// Normalize obsolete editor metadata once on open/focus, before native Rename
	// captures its Undo snapshot. This does not dirty the asset or add an Undo edit.
	bool bHasInstances = false;
	for (const auto& Binding : Scene->GetBindings())
		for (UMovieSceneTrack* Track : Binding.GetTracks())
			if (auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track))
			{
				ComposableCameraSystem::CameraInstanceOutput::ClearLegacyDisplayName(*Owner);
				bHasInstances = true;
			}
	if (bHasInstances) Scene->MarkAsChanged();
}

void FComposableCameraInstanceTrackEditor::Tick(float DeltaTime)
{
	FMovieSceneTrackEditor::Tick(DeltaTime);
	if (bSynchronizing || GIsTransacting || !GEditor || GEditor->IsTransactionActive()) return;
	InitializeBindingNames();
	const auto Seq = GetSequencer();
	auto* Scene = Seq && Seq->GetFocusedMovieSceneSequence() ? Seq->GetFocusedMovieSceneSequence()->GetMovieScene() : nullptr;
	if (!Scene || Scene->IsReadOnly() || Seq->IsReadOnly() || !Seq->GetPlaybackContext()) return;
	// Upgrade only the short-lived prototype root layout. Ordinary legacy Shot Tracks are untouched.
	bool bUpgrade = false;
	for (UMovieSceneTrack* Track : Scene->GetTracks())
		if (auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track); Owner && !AttemptedUpgrades.Contains(Owner))
		{ AttemptedUpgrades.Add(Owner); bUpgrade = true; }
	if (!bUpgrade) return;
	const FScopedTransaction Transaction(LOCTEXT("UpgradeHierarchy", "Upgrade Camera Instance Hierarchy"));
	OnDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

#undef LOCTEXT_NAMESPACE
