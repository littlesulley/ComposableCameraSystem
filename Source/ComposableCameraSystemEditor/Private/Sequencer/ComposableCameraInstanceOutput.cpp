// Copyright 2026 Sulley. All Rights Reserved.
#include "Sequencer/ComposableCameraInstanceOutput.h"
#include "Engine/World.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneFolder.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"

namespace ComposableCameraSystem::CameraInstanceOutput
{
static bool bCreating = false;
bool IsCreating() { return bCreating; }
void ClearLegacyDisplayName(UMovieSceneComposableCameraInstanceTrack& Track)
{
	auto* Scene = Track.GetTypedOuter<UMovieScene>();
	const FGuid Binding = Track.FindObjectBindingGuid();
	if (Scene && !Scene->IsReadOnly() && (Scene->FindSpawnable(Binding) || Scene->FindPossessable(Binding)))
		Scene->SetObjectDisplayName(Binding, FText::GetEmpty());
}

static UComposableCameraLevelSequenceComponent* GetTemplate(TSharedRef<ISequencer> Sequencer, FGuid Guid)
{
	AActor* Actor = Cast<AActor>(MovieSceneHelpers::GetObjectTemplate(Sequencer->GetFocusedMovieSceneSequence(), Guid, Sequencer->GetSharedPlaybackState()));
	return Actor ? Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>() : nullptr;
}

static void RemoveFolderBinding(UMovieSceneFolder& Folder, FGuid Guid)
{
	if (Folder.GetChildObjectBindings().Contains(Guid)) { Folder.Modify(); Folder.RemoveChildObjectBinding(Guid); }
	for (UMovieSceneFolder* Child : Folder.GetChildFolders()) if (Child) RemoveFolderBinding(*Child, Guid);
}

static void RemoveBinding(UMovieSceneSequence& Sequence, FGuid Guid)
{
	UMovieScene* Scene = Sequence.GetMovieScene();
	Sequence.Modify();
	Scene->Modify();
	for (UMovieSceneFolder* Folder : Scene->GetRootFolders()) if (Folder) RemoveFolderBinding(*Folder, Guid);
	Sequence.UnbindPossessableObjects(Guid);
	Scene->RemoveSpawnable(Guid);
	Scene->RemovePossessable(Guid);
}

static bool AttachToBinding(UMovieScene& Scene, UMovieSceneComposableCameraInstanceTrack& Track)
{
	const FGuid Binding = Track.OutputBinding.GetGuid();
	if (!Scene.FindBinding(Binding)) return false;
	if (Track.FindObjectBindingGuid() != Binding)
	{
		Scene.Modify();
		Track.Modify();
		const FText Name = Track.GetDisplayName();
		Scene.RemoveTrack(Track);
		Scene.AddGivenTrack(&Track, Binding);
		// Match FObjectBindingModel::Rename. A display-name override masks native renames.
		if (auto* Spawnable = Scene.FindSpawnable(Binding)) Spawnable->SetName(Name.ToString());
		else if (auto* Possessable = Scene.FindPossessable(Binding)) Possessable->SetName(Name.ToString());
		Track.SetDisplayName(NSLOCTEXT("CameraInstance", "Activation", "Activation"));
	}
	ClearLegacyDisplayName(Track);
	// Upgrade the prototype output folder without touching unrelated user folders.
	const auto Folders = Scene.GetRootFolders();
	for (UMovieSceneFolder* Folder : Folders)
		if (Folder && Folder->GetFolderName() == TEXT("Camera Instance Outputs"))
		{
			RemoveFolderBinding(*Folder, Binding);
			if (Folder->GetChildObjectBindings().IsEmpty() && Folder->GetChildTracks().IsEmpty() && Folder->GetChildFolders().IsEmpty())
			{ Scene.Modify(); Scene.RemoveRootFolder(Folder); }
		}
	return true;
}

bool Ensure(TSharedRef<ISequencer> Sequencer, UMovieSceneComposableCameraInstanceTrack& Track, FString& Error)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	UWorld* World = Cast<UWorld>(Sequencer->GetPlaybackContext());
	if (!Scene || Scene->IsReadOnly() || Sequencer->IsReadOnly() || !World || !Track.Instance || Track.GetOuter() != Scene) return false;
	const FGuid CurrentBinding = Track.FindObjectBindingGuid();
	if (CurrentBinding.IsValid() && UMovieSceneComposableCameraInstanceTrack::Find(Scene, CurrentBinding) == &Track)
		if (auto* Template = GetTemplate(Sequencer, CurrentBinding); Template && Template->bCameraInstanceDriven)
		{
			// Native object-binding duplication copies all child tracks and the spawn template.
			// Reattach to that copied template, keeping the duplicate's parameter lanes together.
			if (Template->CameraInstanceOwner != &Track || Track.OutputBinding.GetGuid() != CurrentBinding)
			{
				Template->Modify(); Track.Modify();
				Template->CameraInstanceOwner = &Track;
				Track.OutputBinding = UE::MovieScene::FRelativeObjectBindingID(CurrentBinding);
				Track.ManagedCut = nullptr;
				for (const auto Object : Sequencer->FindBoundObjects(CurrentBinding, Sequencer->GetFocusedTemplateID()))
					if (auto* Actor = Cast<AActor>(Object.Get()))
						if (auto* Live = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>()) Live->CameraInstanceOwner = &Track;
			}
			return AttachToBinding(*Scene, Track);
		}
	if (auto* Template = GetTemplate(Sequencer, Track.OutputBinding.GetGuid()); Template && Template->CameraInstanceOwner == &Track)
		return AttachToBinding(*Scene, Track);
	if (bCreating) return false;
	TGuardValue<bool> CreatingGuard(bCreating, true);
	Track.Modify();
	Scene->Modify();
	Sequence->Modify();
	// A duplicated track must never inherit the original's Cut or camera identity.
	Track.OutputBinding = FMovieSceneObjectBindingID();
	Track.ManagedCut = nullptr;
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags = RF_Transient;
	auto* Camera = World->SpawnActor<AComposableCameraLevelSequenceActor>(Spawn);
	FGuid Binding;
	if (Camera)
	{
		if (auto* Component = Camera->FindComponentByClass<UComposableCameraLevelSequenceComponent>())
		{
			Component->bCameraInstanceDriven = true;
			Component->CameraInstanceOwner = &Track;
			Component->TypeAssetReference = Track.Instance->Camera;
			Camera->SetActorLabel(Track.GetDisplayName().ToString());
			Binding = Sequencer->MakeNewSpawnable(*Camera, nullptr, false);
		}
		Camera->Destroy();
	}
	if (!Binding.IsValid()) { Error = TEXT("Could not create the managed output camera."); return false; }
	if (const FMovieSceneBinding* Output = Scene->FindBinding(Binding))
	{
		const auto Tracks = Output->GetTracks();
		for (UMovieSceneTrack* Existing : Tracks) if (Existing->IsA<UMovieScene3DTransformTrack>()) Scene->RemoveTrack(*Existing);
	}
	UMovieSceneSpawnTrack* SpawnTrack = Scene->FindTrack<UMovieSceneSpawnTrack>(Binding);
	if (!SpawnTrack) SpawnTrack = Scene->AddTrack<UMovieSceneSpawnTrack>(Binding);
	if (!SpawnTrack)
	{
		RemoveBinding(*Sequence, Binding);
		Error = TEXT("Could not create output camera lifetime track.");
		return false;
	}
	SpawnTrack->Modify();
	SpawnTrack->SetObjectId(Binding);
	SpawnTrack->RemoveAllAnimationData();
	auto* SpawnSection = CastChecked<UMovieSceneSpawnSection>(SpawnTrack->CreateNewSection());
	SpawnSection->SetRange(TRange<FFrameNumber>::All());
	SpawnSection->GetChannel().SetDefault(true);
	SpawnTrack->AddSection(*SpawnSection);
	Track.OutputBinding = UE::MovieScene::FRelativeObjectBindingID(Binding);
	AttachToBinding(*Scene, Track);
	Track.MarkAsChanged();
	return true;
}

bool RemoveOrphans(TSharedRef<ISequencer> Sequencer)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Scene || Scene->IsReadOnly() || Sequencer->IsReadOnly()) return false;
	TArray<FGuid> Remove;
	bool bChanged = false;
	for (const FMovieSceneBinding& Binding : Scene->GetBindings())
	{
		auto* Component = GetTemplate(Sequencer, Binding.GetObjectGuid());
		auto* Owner = Component ? Component->CameraInstanceOwner.Get() : nullptr;
		if (!Owner || Owner->GetOuter() != Scene || Owner->IsInMovieScene()) continue;
		UMovieSceneTrack* Cuts = Scene->GetCameraCutTrack();
		if (Cuts && Owner->ManagedCut && Cuts->HasSection(*Owner->ManagedCut)
			&& Owner->ManagedCut->GetCameraBindingID() == Owner->OutputBinding && !Owner->ManagedCut->IsLocked())
		{
			Cuts->Modify();
			Cuts->RemoveSection(*Owner->ManagedCut);
			bChanged = true;
		}
		// Preserve explicitly authored references instead of leaving broken user Cuts.
		bool bReferenced = false;
		if (Cuts) for (const UMovieSceneSection* Section : Cuts->GetAllSections())
			if (const auto* Cut = Cast<UMovieSceneCameraCutSection>(Section))
				bReferenced |= Cut->GetCameraBindingID() == Owner->OutputBinding;
		if (!bReferenced) Remove.Add(Binding.GetObjectGuid());
	}
	for (const FGuid Guid : Remove) RemoveBinding(*Sequence, Guid);
	return bChanged || !Remove.IsEmpty();
}
}
