// Copyright 2026 Sulley. All Rights Reserved.
#include "Sequencer/ComposableCameraSequencerViewport.h"

#include "Editor.h"
#include "Engine/World.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelEditorViewport.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Sections/MovieSceneSubSection.h"

namespace ComposableCameraSystem::SequencerViewport
{
namespace
{
bool IsComposableClass(const UClass* Class)
{
	return Class && (Class->IsChildOf(AComposableCameraLevelSequenceActor::StaticClass())
		|| Class->IsChildOf(UComposableCameraLevelSequenceComponent::StaticClass()));
}

bool UsesComposableCameras(UMovieSceneSequence* Sequence, TSet<UMovieSceneSequence*>& Visited)
{
	if (!Sequence || Visited.Contains(Sequence)) return false;
	Visited.Add(Sequence);
	UMovieScene* Scene = Sequence->GetMovieScene();
	if (!Scene) return false;
	for (int32 Index = 0; Index < Scene->GetPossessableCount(); ++Index)
	{
		if (IsComposableClass(Scene->GetPossessable(Index).GetPossessedObjectClass())) return true;
	}
	for (int32 Index = 0; Index < Scene->GetSpawnableCount(); ++Index)
	{
		const UObject* Template = Scene->GetSpawnable(Index).GetObjectTemplate();
		if (Template && IsComposableClass(Template->GetClass())) return true;
	}
	for (UMovieSceneSection* Section : Scene->GetAllSections())
	{
		if (!Section) continue;
		if (Section->IsA<UMovieSceneComposableCameraShotSection>()
			|| Section->IsA<UMovieSceneComposableCameraInstanceSection>()) return true;
		if (const auto* SubSection = Cast<UMovieSceneSubSection>(Section))
		{
			if (UsesComposableCameras(SubSection->GetSequence(), Visited)) return true;
		}
	}
	return false;
}

UWorld* GetEditorWorld(UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World && World->WorldType == EWorldType::Editor ? World : nullptr;
}

void ClearUnlockedRoll(UWorld* World)
{
	if (!GEditor || !World) return;
	for (FLevelEditorViewportClient* View : GEditor->GetLevelViewportClients())
	{
		if (!View || View->GetWorld() != World || !View->IsPerspective() || !View->AllowsCinematicControl()) continue;
		// Do not steal a Pilot/Camera Cut view, including an actor pending destruction.
		if (!View->GetActorLock().LockedActor.IsExplicitlyNull()
			|| !View->GetCinematicActorLock().LockedActor.IsExplicitlyNull()) continue;
		if (!FMath::IsNearlyZero(View->GetViewRotation().Roll))
		{
			View->RemoveCameraRoll();
			View->Invalidate();
		}
	}
}
}

void PrepareForOpen(const FSequencerInitParams& Params)
{
	UWorld* World = GetEditorWorld(Params.PlaybackContext.Get(nullptr));
	TSet<UMovieSceneSequence*> Visited;
	if (World && UsesComposableCameras(Params.RootSequence, Visited)) ClearUnlockedRoll(World);
}

void RestoreBeforeShutdown(TConstArrayView<TWeakPtr<ISequencer>> Sequencers)
{
	TArray<TSharedPtr<ISequencer>> ToRestore;
	// Release newer sessions first. Disable every eligible session before restoring
	// any state, so the final editor tick cannot reapply a cinematic camera.
	for (int32 Index = Sequencers.Num() - 1; Index >= 0; --Index)
	{
		TSharedPtr<ISequencer> Sequencer = Sequencers[Index].Pin();
		if (!Sequencer || !GetEditorWorld(Sequencer->GetPlaybackContext())) continue;
		TSet<UMovieSceneSequence*> Visited;
		if (!UsesComposableCameras(Sequencer->GetRootMovieSceneSequence(), Visited)) continue;
		Sequencer->SetPerspectiveViewportCameraCutEnabled(false);
		ToRestore.Add(MoveTemp(Sequencer));
	}
	for (const TSharedPtr<ISequencer>& Sequencer : ToRestore)
	{
		// Use native pre-animated restoration, which also releases cinematic locks
		// and respects Restore Original Viewport on Camera Cut Unlock. Do not close
		// asset tabs here: UE still needs to record them for startup restoration.
		Sequencer->RestorePreAnimatedState();
		ClearUnlockedRoll(GetEditorWorld(Sequencer->GetPlaybackContext()));
	}
}
}
