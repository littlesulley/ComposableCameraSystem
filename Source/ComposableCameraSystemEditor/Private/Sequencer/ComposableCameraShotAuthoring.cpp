// Copyright 2026 Sulley. All Rights Reserved.
#include "Sequencer/ComposableCameraShotAuthoring.h"

#include "Editors/ComposableCameraShotTemplates.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Misc/ScopeExit.h"
#include "ISequencer.h"
#include "LevelSequence/ComposableCameraLevelSequenceShotActor.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnableAnnotation.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotAuthoring"
namespace ComposableCameraSystem::ShotAuthoring
{
bool ResolveSubjectBinding(ISequencer& Sequencer, AActor* Actor, bool bCreatePossessable,
	FMovieSceneObjectBindingID& Binding, FString& Reason)
{
	Binding = FMovieSceneObjectBindingID();
	if (!Actor) { Reason = TEXT("Choose a valid subject actor."); return false; }
	if (const auto Annotation = FMovieSceneSpawnableAnnotation::Find(Actor); Annotation.IsSet())
	{
		for (const TWeakObjectPtr<UObject> Object : Sequencer.FindBoundObjects(Annotation->ObjectBindingID, Annotation->SequenceID))
		{
			if (Object.Get() == Actor)
			{
				Binding = UE::MovieScene::FRelativeObjectBindingID(Sequencer.GetFocusedTemplateID(),
					Annotation->SequenceID, Annotation->ObjectBindingID, Sequencer);
				return true;
			}
		}
		Reason = TEXT("This subject is spawned by another Sequence player. Open its owning sequence or choose a level actor.");
		return false;
	}
	FGuid Guid = Sequencer.FindObjectId(*Actor, Sequencer.GetFocusedTemplateID());
	if (!Guid.IsValid())
	{
		const UMovieSceneSequence* Sequence = Sequencer.GetFocusedMovieSceneSequence();
		if (!Sequence || !Sequence->CanPossessObject(*Actor, Sequencer.GetPlaybackContext()))
		{ Reason = TEXT("This sequence cannot bind the selected subject."); return false; }
		if (!bCreatePossessable) return true;
		Guid = Sequencer.GetHandleToObject(Actor, true);
	}
	if (!Guid.IsValid()) { Reason = TEXT("Could not create the subject binding."); return false; }
	Binding = UE::MovieScene::FRelativeObjectBindingID(Guid);
	return true;
}

bool CanCreateRange(const UMovieScene& MovieScene, const FGuid& CameraBinding,
	const TRange<FFrameNumber>& Range, FString& Reason)
{
	if (MovieScene.IsReadOnly() || Range.IsEmpty() || !Range.HasLowerBound() || !Range.HasUpperBound())
	{
		Reason = TEXT("Sequence is read-only or the requested range is invalid.");
		return false;
	}
	if (const UMovieSceneCameraCutTrack* Cuts = Cast<UMovieSceneCameraCutTrack>(MovieScene.GetCameraCutTrack()))
	{
		const FMovieSceneObjectBindingID LocalBinding = UE::MovieScene::FRelativeObjectBindingID(CameraBinding);
		for (const UMovieSceneSection* Section : Cuts->GetAllSections())
		{
			if (!Section || !Section->IsActive() || TRange<FFrameNumber>::Intersection(Section->GetRange(), Range).IsEmpty()) continue;
			const UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section);
			if (!Cut || !CameraBinding.IsValid() || Cut->GetCameraBindingID() != LocalBinding || !Cut->GetRange().Contains(Range))
			{
				Reason = TEXT("Camera Cuts already occupy this range. Move the playhead to a gap or use the current Shot camera inside its existing cut.");
				return false;
			}
		}
	}
	if (CameraBinding.IsValid())
	{
		for (const FMovieSceneBinding& Binding : MovieScene.GetBindings())
		{
			if (Binding.GetObjectGuid() != CameraBinding) continue;
			for (const UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (!Track || !Track->IsA<UMovieSceneComposableCameraShotTrack>()) continue;
				for (const UMovieSceneSection* Section : Track->GetAllSections())
				{
					if (Section && !TRange<FFrameNumber>::Intersection(Section->GetRange(), Range).IsEmpty())
					{
						Reason = TEXT("The Shot Track already has a clip in this range. Move the playhead after it.");
						return false;
					}
				}
			}
		}
	}
	return true;
}

bool EnsureCameraSpawnRange(UMovieScene& Scene, const FGuid& CameraGuid, const TRange<FFrameNumber>& Range)
{
	if (Scene.IsReadOnly() || !CameraGuid.IsValid() || Range.IsEmpty() || !Range.HasLowerBound() || !Range.HasUpperBound()) return false;
	UMovieSceneSpawnTrack* Spawn = Scene.FindTrack<UMovieSceneSpawnTrack>(CameraGuid);
	if (!Spawn && !Scene.FindSpawnable(CameraGuid)) return true; // Ordinary Possessable.
	if (!Spawn) Spawn = Scene.AddTrack<UMovieSceneSpawnTrack>(CameraGuid);
	if (!Spawn) return false;
	int32 Priority = 0;
	for (const UMovieSceneSection* Existing : Spawn->GetAllSections())
	{
		Priority = FMath::Max(Priority, Existing->GetOverlapPriority());
		const auto* Section = Cast<UMovieSceneSpawnSection>(Existing);
		if (!Section || !Section->IsActive() || !Section->GetRange().Contains(Range)) continue;
		const auto& Channel = Section->GetChannel();
		bool Value = false;
		Channel.Evaluate(Range.GetLowerBoundValue(), Value);
		if (!Value) continue;
		bool bStaysTrue = true;
		for (int32 Index = 0; Index < Channel.GetTimes().Num(); ++Index)
			if (Range.Contains(Channel.GetTimes()[Index]) && !Channel.GetValues()[Index]) { bStaysTrue = false; break; }
		if (bStaysTrue && Spawn->GetAllSections().Num() == 1) return true;
	}
	if (Priority == MAX_int32) return false;
	Spawn->Modify(); Spawn->SetObjectId(CameraGuid);
	auto* Section = Cast<UMovieSceneSpawnSection>(Spawn->CreateNewSection());
	if (!Section) return false;
	Section->SetRange(Range); Section->SetRowIndex(0); Section->SetOverlapPriority(Priority + 1);
	Section->GetChannel().SetDefault(true); Spawn->AddSection(*Section);
	return true;
}

namespace
{
bool EnsureCut(UMovieScene& Scene, const FGuid& CameraGuid, const TRange<FFrameNumber>& Range)
{
	UMovieSceneCameraCutTrack* Cuts = Cast<UMovieSceneCameraCutTrack>(Scene.GetCameraCutTrack());
	if (!Cuts) Cuts = Cast<UMovieSceneCameraCutTrack>(Scene.AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	if (!Cuts) return false;
	for (UMovieSceneSection* Existing : Cuts->GetAllSections())
	{
		if (Existing->IsActive() && Existing->GetRange().Contains(Range)) return true;
	}
	Cuts->Modify();
	UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
	if (!Cut) return false;
	Cut->SetRange(Range);
	Cut->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(CameraGuid));
	Cuts->AddSection(*Cut); // AddNewCameraCut auto-arranges neighbours; explicit sections preserve existing edits.
	return true;
}

UMovieSceneComposableCameraShotTrack* FindTrack(UMovieScene& Scene, const FGuid& Guid)
{
	for (const FMovieSceneBinding& Binding : Scene.GetBindings())
		if (Binding.GetObjectGuid() == Guid)
			for (UMovieSceneTrack* Track : Binding.GetTracks())
				if (auto* ShotTrack = Cast<UMovieSceneComposableCameraShotTrack>(Track)) return ShotTrack;
	return nullptr;
}

void SelectCreated(TSharedRef<ISequencer> Sequencer, UMovieSceneComposableCameraShotSection* Section)
{
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	Sequencer->SetLocalTimeDirectly(Section->GetInclusiveStartFrame());
	Sequencer->EmptySelection();
	Sequencer->SelectSection(Section);
	Sequencer->ThrobSectionSelection();
	Sequencer->SetPerspectiveViewportCameraCutEnabled(true);
}
}

UMovieSceneComposableCameraShotSection* CreateInSequence(TSharedRef<ISequencer> Sequencer,
	const FComposableCameraShot& Shot, const FText& Label, float DurationSeconds,
	bool bDialogue, bool bSpawnable, UMovieSceneComposableCameraShotSection* ReuseSource, FString& Reason)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!Scene || !World || Shot.Targets.Num() == 0 || (bDialogue && Shot.Targets.Num() != 2))
	{
		Reason = TEXT("Open a Level Sequence and bind subjects first. Dialogue creation needs two subjects.");
		return nullptr;
	}
	TArray<AActor*> Actors;
	for (const FComposableCameraShotTarget& Target : Shot.Targets)
	{
		AActor* Actor = Target.Target.Actor.Get();
		if (!Actor || Actor->GetWorld() != World)
		{
			Reason = TEXT("Every subject must resolve in the current editor world. Scrub inside its spawn range first.");
			return nullptr;
		}
		Actors.Add(Actor);
	}
	FGuid CameraGuid;
	UMovieSceneComposableCameraShotTrack* Track = nullptr;
	FFrameNumber Start = Sequencer->GetLocalTime().Time.FloorToFrame();
	if (ReuseSource && ReuseSource->GetTypedOuter<UMovieScene>() == Scene)
	{
		Track = ReuseSource->GetTypedOuter<UMovieSceneComposableCameraShotTrack>();
		CameraGuid = Track ? Track->FindObjectBindingGuid() : FGuid();
		if (ReuseSource->GetRange().Contains(Start) && ReuseSource->HasEndFrame()) Start = ReuseSource->GetExclusiveEndFrame();
	}
	const int32 Count = bDialogue ? 3 : 1;
	const double Frames = Scene->GetTickResolution().AsDecimal() * FMath::Clamp(DurationSeconds, .1f, 3600.f);
	if (!FMath::IsFinite(Frames) || Frames <= 0 || Frames > MAX_int32 / Count) { Reason = TEXT("Clip duration or sequence frame rate is invalid."); return nullptr; }
	const int32 Length = FMath::Max(1, FMath::CeilToInt(Frames));
	if (static_cast<int64>(Start.Value) + static_cast<int64>(Length) * Count > MAX_int32) { Reason = TEXT("Clip range exceeds sequence frame-number limits."); return nullptr; }
	const TRange<FFrameNumber> Range(Start, Start + Length * Count);
	if (!CanCreateRange(*Scene, CameraGuid, Range, Reason)) return nullptr;
	if (!CameraGuid.IsValid() && bSpawnable && !Sequence->AllowsSpawnableObjects())
	{
		Reason = TEXT("This sequence does not support Spawnables. Disable Sequence-owned camera.");
		return nullptr;
	}
	for (AActor* Actor : Actors)
	{
		FMovieSceneObjectBindingID Binding;
		if (!ResolveSubjectBinding(*Sequencer, Actor, false, Binding, Reason)) return nullptr;
	}
	if (GEditor->IsTransactionActive()) { Reason = TEXT("Finish the current edit gesture before creating shots."); return nullptr; }
	TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("Create", "Create Composable Camera Shots"));
	bool bCommitted = false;
	ON_SCOPE_EXIT
	{
		Transaction.Reset();
		if (!bCommitted)
		{
			// End and undo only this helper's transaction. Cancelling a transaction does not revert its writes.
			GEditor->UndoTransaction();
			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
		}
	};
	Scene->Modify();
	Sequence->Modify();
	if (!CameraGuid.IsValid())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags = bSpawnable ? RF_Transient : RF_Transactional;
		if (!bSpawnable && World->GetCurrentLevel()) World->GetCurrentLevel()->Modify();
		AComposableCameraLevelSequenceShotActor* Camera = World->SpawnActor<AComposableCameraLevelSequenceShotActor>(Params);
		if (!Camera) { Reason = TEXT("Could not create the Shot camera."); return nullptr; }
		Camera->SetActorLabel(TEXT("Shot Camera"));
		if (bSpawnable)
		{
			CameraGuid = Sequencer->MakeNewSpawnable(*Camera, nullptr, false);
			Camera->Destroy();
		}
		else
		{
			CameraGuid = Sequencer->GetHandleToObject(Camera, true);
		}
		if (!CameraGuid.IsValid()) { Reason = TEXT("Could not bind the Shot camera."); return nullptr; }
		// Only a newly-created camera's generated Transform tracks may be removed.
		for (const FMovieSceneBinding& Binding : Scene->GetBindings())
		{
			if (Binding.GetObjectGuid() != CameraGuid) continue;
			const TArray<UMovieSceneTrack*> Tracks = Binding.GetTracks();
			for (UMovieSceneTrack* Existing : Tracks)
				if (Existing->IsA<UMovieScene3DTransformTrack>()) Scene->RemoveTrack(*Existing);
			break;
		}
		Sequencer->SetDisplayName(CameraGuid, LOCTEXT("Camera", "Shot Camera"));
	}
	if (!Track) Track = FindTrack(*Scene, CameraGuid);
	if (!Track)
	{
		Track = Scene->AddTrack<UMovieSceneComposableCameraShotTrack>(CameraGuid);
		if (Track) Track->SetDisplayName(LOCTEXT("Track", "Camera Shots"));
	}
	if (!Track) { Reason = TEXT("Could not create the Shot Track."); return nullptr; }
	if (!EnsureCameraSpawnRange(*Scene, CameraGuid, Range)) { Reason = TEXT("Could not extend the camera's spawn coverage."); return nullptr; }
	Track->Modify();
	TArray<FComposableCameraShotTargetActorOverride> Bindings;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		if (ReuseSource && ReuseSource->GetTypedOuter<UMovieScene>() == Scene)
		{
			const auto* Existing = ReuseSource->TargetActorOverrides.FindByPredicate([Index](const auto& Binding) { return Binding.TargetIndex == Index && Binding.Binding.IsValid(); });
			if (Existing) { Bindings.Add(*Existing); continue; } // Preserve full relative binding IDs, including parent-sequence subjects.
		}
		FMovieSceneObjectBindingID SubjectBinding;
		if (!ResolveSubjectBinding(*Sequencer, Actors[Index], true, SubjectBinding, Reason)) return nullptr;
		FComposableCameraShotTargetActorOverride& Binding = Bindings.AddDefaulted_GetRef();
		Binding.TargetIndex = Index;
		Binding.Binding = SubjectBinding;
	}
	UMovieSceneComposableCameraShotSection* First = nullptr;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UMovieSceneComposableCameraShotSection* Section = Cast<UMovieSceneComposableCameraShotSection>(Track->CreateNewSection());
		if (!Section) { Reason = TEXT("Could not create the Shot Section."); return nullptr; }
		Section->SetRange(TRange<FFrameNumber>(Start + Length * Index, Start + Length * (Index + 1)));
		Section->SetRowIndex(0);
		Section->InlineShot = Shot;
		if (bDialogue)
		{
			const EComposableShotTemplate Template = Index == 0 ? EComposableShotTemplate::TwoShot
				: Index == 1 ? EComposableShotTemplate::ShoulderLeft : EComposableShotTemplate::ReverseShoulder;
			BuildTemplate(Template, Shot.Targets, Section->InlineShot);
			Section->ShotLabel = TemplateLabel(Template).ToString();
		}
		else Section->ShotLabel = Label.ToString();
		Section->TargetActorOverrides = Bindings;
		for (FComposableCameraShotTarget& Target : Section->InlineShot.Targets) Target.Target.Actor.Reset();
		Track->AddSection(*Section);
		if (!First) First = Section;
	}
	if (!EnsureCut(*Scene, CameraGuid, Range)) { Reason = TEXT("Could not create Camera Cuts."); return nullptr; }
	if (!Scene->IsPlaybackRangeLocked()) Scene->SetPlaybackRange(TRange<FFrameNumber>::Hull(Scene->GetPlaybackRange(), Range));
	bCommitted = true;
	Transaction.Reset();
	SelectCreated(Sequencer, First);
	return First;
}

UMovieSceneComposableCameraShotSection* DuplicateInSequence(TSharedRef<ISequencer> Sequencer,
	UMovieSceneComposableCameraShotSection& Source, FString& Reason)
{
	UMovieScene* Scene = Source.GetTypedOuter<UMovieScene>();
	UMovieSceneComposableCameraShotTrack* Track = Source.GetTypedOuter<UMovieSceneComposableCameraShotTrack>();
	if (!Scene || !Track || Sequencer->GetFocusedMovieSceneSequence() != Scene->GetOuter()
		|| !Source.HasStartFrame() || !Source.HasEndFrame() || Source.IsLocked())
	{ Reason = TEXT("Open the shot's owning sequence; the source clip must have a finite range and be unlocked."); return nullptr; }
	const FFrameNumber Start = Source.GetExclusiveEndFrame();
	const int64 Length = static_cast<int64>(Start.Value) - Source.GetInclusiveStartFrame().Value;
	if (Length <= 0 || static_cast<int64>(Start.Value) + Length > MAX_int32)
	{ Reason = TEXT("Clip range exceeds sequence frame-number limits."); return nullptr; }
	const TRange<FFrameNumber> Range(Start, FFrameNumber(static_cast<int32>(Start.Value + Length)));
	if (!CanCreateRange(*Scene, Track->FindObjectBindingGuid(), Range, Reason)) return nullptr;
	if (!GEditor || GEditor->IsTransactionActive()) { Reason = TEXT("Finish the current edit gesture before duplicating shots."); return nullptr; }
	TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("Duplicate", "Duplicate Camera Shot"));
	bool bCommitted = false;
	ON_SCOPE_EXIT { Transaction.Reset(); if (!bCommitted) { GEditor->UndoTransaction(); Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged); } };
	Scene->Modify();
	Track->Modify();
	if (!EnsureCameraSpawnRange(*Scene, Track->FindObjectBindingGuid(), Range)) { Reason = TEXT("Could not extend the camera's spawn coverage."); return nullptr; }
	UMovieSceneComposableCameraShotSection* Copy = DuplicateObject<UMovieSceneComposableCameraShotSection>(&Source, Track);
	Copy->SetRange(Range);
	Copy->SetRowIndex(Source.GetRowIndex());
	Track->AddSection(*Copy);
	if (!EnsureCut(*Scene, Track->FindObjectBindingGuid(), Range)) { Reason = TEXT("Could not create Camera Cuts."); return nullptr; }
	if (!Scene->IsPlaybackRangeLocked()) Scene->SetPlaybackRange(TRange<FFrameNumber>::Hull(Scene->GetPlaybackRange(), Range));
	bCommitted = true; Transaction.Reset();
	SelectCreated(Sequencer, Copy);
	return Copy;
}
}
#undef LOCTEXT_NAMESPACE
