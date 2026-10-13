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
	TRange<FFrameNumber> Range;
	if (!MakeCreationRange(Scene->GetTickResolution(), Start, DurationSeconds, Range, Error)) return nullptr;
	// A drop into an occupied edit is still a valid instance creation. Existing cuts are
	// preserved; Camera Cuts can select this output explicitly once the range is free.
	const bool bCreateCut = !HasCutConflict(*Scene, Range);
	const FScopedTransaction Transaction(LOCTEXT("Create", "Create Camera Instance"));
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
	if (bCreateCut)
	{
		auto* Cuts = Cast<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
		if (!Cuts) Cuts = Cast<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
		if (Cuts)
		{
			Cuts->Modify();
			Track->ManagedCut = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
			Track->ManagedCut->SetRange(Range);
			Track->ManagedCut->SetCameraBindingID(Track->OutputBinding);
			Cuts->AddSection(*Track->ManagedCut);
		}
	}
	else Error = TEXT("Camera instance created. Existing Camera Cuts occupy this interval and were preserved; assign its output in Camera Cuts to view it.");
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
		if (!HasCutConflict(*Scene, Range))
		{
			auto* Cuts = Cast<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
			if (!Cuts) Cuts = Cast<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
			if (Cuts)
			{
				Cuts->Modify();
				Copy->ManagedCut = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
				Copy->ManagedCut->SetRange(Range);
				Copy->ManagedCut->SetCameraBindingID(Copy->OutputBinding);
				Cuts->AddSection(*Copy->ManagedCut);
			}
		}
		else Error = TEXT("Camera instance duplicated. Existing Camera Cuts were preserved; assign the new output in Camera Cuts to view it.");
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
	TArray<FComposableCameraInstanceField> Fields;
	Track.Instance->GatherFields(Fields);
	const auto* Field = Fields.FindByPredicate([&](const auto& Item) { return Item.Path == Input; });
	const auto* Property = Field ? CastField<FObjectPropertyBase>(Field->Property) : nullptr;
	if (!Property || !Property->PropertyClass->IsChildOf(AActor::StaticClass())) return false;
	const UClass* Class = LocalBinding.IsValid() ? MovieSceneHelpers::GetBoundObjectClass(Sequence, LocalBinding) : nullptr;
	if (LocalBinding.IsValid() && (!Class || !Class->IsChildOf(Property->PropertyClass))) return false;
	Track.Modify();
	Track.Instance->Modify();
	Track.Instance->RefreshSubjectSlots();
	auto* Slot = Track.Instance->Subjects.FindByPredicate([&](const auto& Item) { return Item.Input == Input; });
	if (!Slot) return false;
	Slot->Binding = LocalBinding.IsValid() ? FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(LocalBinding)) : FMovieSceneObjectBindingID();
	Field->WriteObject(nullptr);
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
			TArray<FComposableCameraInstanceField> Fields;
			Instance->Instance->GatherFields(Fields);
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
	if (const auto* Sequence = Sequencer->GetFocusedMovieSceneSequence())
	{
		const auto* Scene = Sequence->GetMovieScene();
		for (auto* Track : Scene->GetTracks()) if (auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track)) KnownInstances.Add(Owner);
		for (const auto& Binding : Scene->GetBindings())
			for (auto* Track : Binding.GetTracks()) if (auto* Owner = Cast<UMovieSceneComposableCameraInstanceTrack>(Track)) KnownInstances.Add(Owner);
	}
}
FComposableCameraInstanceTrackEditor::~FComposableCameraInstanceTrackEditor()
{
	if (const auto Sequencer = GetSequencer()) Sequencer->OnMovieSceneDataChanged().Remove(DataChangedHandle);
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
void FComposableCameraInstanceTrackEditor::OnDataChanged(EMovieSceneDataChangeType)
{
	// Never create a second Undo entry from a notification, or mutate while Undo/Redo is
	// restoring objects. Explicit repair remains available for external, non-transactional edits.
	if (bSynchronizing || ComposableCameraSystem::CameraInstanceOutput::IsCreating()
		|| GIsTransacting || !GEditor || !GEditor->IsTransactionActive()) return;
	const auto Sequencer = GetSequencer();
	UMovieScene* Scene = Sequencer && Sequencer->GetFocusedMovieSceneSequence() ? Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene() : nullptr;
	if (!Scene || Scene->IsReadOnly() || Sequencer->IsReadOnly()) return;
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
	KnownInstances.Reset();
	for (UMovieSceneTrack* Root : InstanceTracks)
	{
		auto* Track = Cast<UMovieSceneComposableCameraInstanceTrack>(Root);
		if (!Track) continue;
		KnownInstances.Add(Track);
		const FMovieSceneObjectBindingID PreviousOutput = Track->OutputBinding;
		const FGuid PreviousParent = Track->FindObjectBindingGuid();
		FString Error;
		if (!ComposableCameraSystem::CameraInstanceOutput::Ensure(Sequencer.ToSharedRef(), *Track, Error)) continue;
		bChanged |= Track->OutputBinding != PreviousOutput || Track->FindObjectBindingGuid() != PreviousParent;
		if (!Track->ManagedCut || Track->GetAllSections().Num() != 1 || Track->ManagedCut->IsLocked()) continue;
		const UMovieSceneSection* Section = Track->GetAllSections()[0];
		if (!Section || Track->ManagedCut->GetCameraBindingID() != Track->OutputBinding) continue;
		const UMovieSceneTrack* Cuts = Scene->GetCameraCutTrack();
		if (!Cuts || !Cuts->HasSection(*Track->ManagedCut)) continue;
		if (Track->ManagedCut->GetRange() != Section->GetRange()
			&& !ComposableCameraSystem::CameraInstanceAuthoring::HasCutConflict(*Scene, Section->GetRange(), Track->ManagedCut))
		{
			Track->ManagedCut->Modify();
			Track->ManagedCut->SetRange(Section->GetRange());
			bChanged = true;
		}
	}
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
		TArray<FComposableCameraInstanceField> Fields;
		WeakOwner->Instance->GatherFields(Fields);
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
