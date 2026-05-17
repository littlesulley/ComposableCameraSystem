// Copyright 2026 Sulley. All Rights Reserved.

#include "AsyncActions/AsyncPlayCutsceneSequence.h"

#include "Engine/Engine.h"
#include "ComposableCameraSystemModule.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "GameFramework/PlayerController.h"

UAsyncPlayCutsceneSequence* UAsyncPlayCutsceneSequence::Create(
	UObject* WorldContextObject,
	ULevelSequence* InLevelSequence,
	FName ContextName,
	UComposableCameraTransitionDataAsset* InEnterTransition,
	FMovieSceneSequencePlaybackSettings PlaybackSettings)
{
	UAsyncPlayCutsceneSequence* Action = NewObject<UAsyncPlayCutsceneSequence>();
	Action->LevelSequence = InLevelSequence;
	Action->CutsceneContextName = ContextName;
	Action->EnterTransition = InEnterTransition;
	Action->CachedPlaybackSettings = PlaybackSettings;
	Action->CachedWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UAsyncPlayCutsceneSequence::Activate()
{
	if (!LevelSequence)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: LevelSequence is null. Action cancelled."));
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	if (CutsceneContextName == NAME_None)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: ContextName is NAME_None. Action cancelled."));
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	// Find the CCS PCM.
	UWorld* World = CachedWorld.Get();
	if (!World)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: No valid world. Action cancelled."));
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: No player controller. Action cancelled."));
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	CachedPCM = Cast<AComposableCameraPlayerCameraManager>(PC->PlayerCameraManager);
	if (!CachedPCM.IsValid())
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: PlayerCameraManager is not AComposableCameraPlayerCameraManager. Action cancelled."));
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	// 1. Push the cutscene context onto the context stack.
	//    We push an empty context (no camera yet). The first CameraCut in the LS
	//    will fire SetViewTarget on the PCM, which creates a proxy camera and
	//    activates it in this cutscene context via implicit activation.
	//
	//    For the inter-context transition (gameplay->cutscene), we use
	//    ActivateNewCamera with a dummy camera that will be immediately replaced
	//    by the first LS camera's SetViewTarget call. The inter-context transition
	//    is between the gameplay context and the cutscene context.
	AComposableCameraCameraBase* DummyCamera = CachedPCM->ActivateNewCamera(
		AComposableCameraCameraBase::StaticClass(),
		EnterTransition.Get(),
		FComposableCameraActivateParams(),
		FOnCameraFinishConstructed(),
		CutsceneContextName);

	if (!DummyCamera)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: Failed to push cutscene context '%s'. Action cancelled."),
			*CutsceneContextName.ToString());
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	// Name the initial camera for debug identification.
	{
		const FName DesiredName(*FString::Printf(TEXT("CutsceneInitial_%s"),
			*LevelSequence->GetName()));
		const FName UniqueName = MakeUniqueObjectName(
			DummyCamera->GetOuter(), DummyCamera->GetClass(), DesiredName);
		DummyCamera->Rename(*UniqueName.ToString());
#if WITH_EDITOR
		DummyCamera->SetActorLabel(FString::Printf(TEXT("CutsceneInitial_%s"),
			*LevelSequence->GetName()));
#endif
	}

	// 2. Create the LS player. Engine CameraCut calls will reach the PCM's
	//    SetViewTarget override, which handles proxy camera creation.
	//    Force bDisableCameraCuts = false so CameraCut events flow through
	//    CCS's implicit activation path regardless of user settings.
	FMovieSceneSequencePlaybackSettings PlaybackSettings = CachedPlaybackSettings;
	PlaybackSettings.bDisableCameraCuts = false;

	ALevelSequenceActor* OutActor = nullptr;
	SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
		World, LevelSequence.Get(), PlaybackSettings, OutActor);
	SequenceActor = OutActor;

	if (!SequencePlayer || !SequenceActor)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("PlayCutsceneSequence: Failed to create LevelSequencePlayer. Cleaning up."));
		CleanUp();
		OnFinished.Broadcast();
		SetReadyToDestroy();
		return;
	}

	// Bind the finish delegate for sequences that will end (non-infinite-loop).
	// When the LS finishes:
	//   - bPauseAtEnd=false ->pop context, clean up, broadcast OnFinished.
	//   - bPauseAtEnd=true  ->broadcast OnFinished but keep the cutscene context
	//     alive so the camera holds the final pose. The caller must eventually
	//     call StopCutsceneSequence() to tear down.
	if (CachedPlaybackSettings.LoopCount.Value != -1)
	{
		SequencePlayer->OnFinished.AddDynamic(this, &UAsyncPlayCutsceneSequence::OnSequenceFinished);
	}

	// 3. Start playback. The engine's CameraCut track will call SetViewTarget
	//    on the PCM for each camera cut, creating proxy cameras in the cutscene
	//    context with appropriate transitions.
	SequencePlayer->Play();
	bIsActive = true;

	UE_LOG(LogComposableCameraSystem, Log,
		TEXT("PlayCutsceneSequence: Started '%s' in context '%s'."),
		*LevelSequence->GetName(), *CutsceneContextName.ToString());
}

void UAsyncPlayCutsceneSequence::StopCutsceneSequence(UComposableCameraTransitionDataAsset* ExitTransition)
{
	if (!bIsActive)
	{
		return;
	}

	if (SequencePlayer)
	{
		SequencePlayer->Stop();
	}

	CleanUp(ExitTransition);
	SetReadyToDestroy();
}

void UAsyncPlayCutsceneSequence::OnSequenceFinished()
{
	if (!bIsActive)
	{
		return;
	}

	UE_LOG(LogComposableCameraSystem, Log,
		TEXT("PlayCutsceneSequence: Sequence '%s' finished (PauseAtEnd=%s)."),
		LevelSequence ? *LevelSequence->GetName() : TEXT("(null)"),
		CachedPlaybackSettings.bPauseAtEnd ? TEXT("true") : TEXT("false"));

	if (CachedPlaybackSettings.bPauseAtEnd)
	{
		// The LS player is now paused at the last frame. Keep the cutscene
		// context alive so the camera holds on the final pose. The user
		// calls StopCutsceneSequence() when they're ready to leave.
		// We still broadcast OnFinished so Blueprint knows the sequence
		// reached its end and can react (e.g. show skip UI, start a timer).
		OnFinished.Broadcast();
	}
	else
	{
		CleanUp();
		OnFinished.Broadcast();
		SetReadyToDestroy();
	}
}

void UAsyncPlayCutsceneSequence::CleanUp(UComposableCameraTransitionDataAsset* PopTransition)
{
	bIsActive = false;

	// 1. Pop the cutscene context. This triggers an inter-context transition back to gameplay.
	if (CachedPCM.IsValid())
	{
		CachedPCM->PopCameraContext(CutsceneContextName, PopTransition);
	}

	// 2. Clean up the LS player.
	if (SequencePlayer)
	{
		SequencePlayer->OnFinished.RemoveDynamic(this, &UAsyncPlayCutsceneSequence::OnSequenceFinished);
		SequencePlayer->Stop();
		SequencePlayer = nullptr;
	}

	if (SequenceActor)
	{
		SequenceActor->Destroy();
		SequenceActor = nullptr;
	}
}
