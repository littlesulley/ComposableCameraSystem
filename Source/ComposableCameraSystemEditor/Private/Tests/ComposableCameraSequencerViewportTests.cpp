// Copyright 2026 Sulley. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Sequencer/ComposableCameraSequencerViewport.h"
#include "CineCameraActor.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelEditorViewport.h"
#include "LevelSequence.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSubSection.h"
#include "SequencerSettings.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class FSequencerTestViewport final : public FLevelEditorViewportClient
{
public:
	explicit FSequencerTestViewport(UWorld* World)
		: FLevelEditorViewportClient(nullptr), TestWorld(World)
	{
		SetViewportType(LVT_Perspective);
		SetAllowCinematicControl(true);
	}
	virtual UWorld* GetWorld() const override { return TestWorld.Get(); }
private:
	TWeakObjectPtr<UWorld> TestWorld;
};

struct FSequencerViewportFixture
{
	TStrongObjectPtr<UWorld> World{UWorld::CreateWorld(EWorldType::Editor, false)};
	TStrongObjectPtr<ULevelSequence> Sequence{NewObject<ULevelSequence>()};
	TUniquePtr<FSequencerTestViewport> View;
	TSharedPtr<ISequencer> Sequencer;
	TWeakObjectPtr<ACineCameraActor> Camera;
	bool bPreviousRestore = true;
	bool bClosed = false;

	explicit FSequencerViewportFixture(bool bIncludeCCS = true)
	{
		GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World.Get());
		View = MakeUnique<FSequencerTestViewport>(World.Get());
		Sequence->Initialize();
		auto* Scene = Sequence->GetMovieScene();
		Scene->SetTickResolutionDirectly(FFrameRate(30, 1));
		Scene->SetPlaybackRange(0, 300);
		Camera = World->SpawnActor<ACineCameraActor>(FVector(1000, 200, 300), FRotator(-12, 30, 25));
		const FGuid Binding = Scene->AddPossessable(TEXT("Rolled Camera"), ACineCameraActor::StaticClass());
		Sequence->BindPossessableObject(Binding, *Camera.Get(), World.Get());
		auto* Cuts = Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
		auto* Cut = CastChecked<UMovieSceneCameraCutSection>(Cuts->CreateNewSection());
		Cut->SetRange(TRange<FFrameNumber>(0, 300));
		Cut->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(Binding));
		Cuts->AddSection(*Cut);
		// A legacy CCS section scopes the session; keep it inactive so this regression
		// isolates native Camera Cut capture/restore from runtime Shot solving.
		if (bIncludeCCS)
		{
			UMovieSceneTrack* Shots = Scene->AddTrack(UMovieSceneComposableCameraShotTrack::StaticClass(), Binding);
			UMovieSceneSection* Shot = Shots->CreateNewSection();
			Shot->SetRange(TRange<FFrameNumber>(0, 300));
			Shot->SetIsActive(false);
			Shots->AddSection(*Shot);
		}
	}
	void Open(bool bRestore = true)
	{
		FSequencerInitParams Params;
		Params.RootSequence = Sequence.Get(); Params.PlaybackContext = World.Get();
		Params.bEditWithinLevelEditor = true;
		Params.ViewParams.UniqueName = TEXT("CCSViewportLifecycleRegression");
		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
		bPreviousRestore = Sequencer->GetSequencerSettings()->GetRestoreOriginalViewportOnCameraCutUnlock();
		Sequencer->GetSequencerSettings()->SetRestoreOriginalViewportOnCameraCutUnlock(bRestore);
		Sequencer->SetPerspectiveViewportCameraCutEnabled(true);
	}
	void EvaluateCut()
	{
		Sequencer->ForceEvaluate();
		View->UpdateViewForLockedActor();
	}
	void Close()
	{
		if (Sequencer && !bClosed)
		{
			Sequencer->Close();
			Sequencer->GetSequencerSettings()->SetRestoreOriginalViewportOnCameraCutUnlock(bPreviousRestore);
			bClosed = true;
		}
	}
	~FSequencerViewportFixture()
	{
		Close(); Sequencer.Reset(); View.Reset();
		GEngine->DestroyWorldContext(World.Get()); World->DestroyWorld(false);
	}
};

bool CanRunViewportRegression()
{
	return GEditor && GEngine && !GEditor->PlayWorld && FSlateApplication::IsInitialized();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequencerViewportStartupRollTest,
	"ComposableCameraSystem.Editor.SequencerViewport.StartupRollRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSequencerViewportStartupRollTest::RunTest(const FString&)
{
	if (!CanRunViewportRegression()) return false;
	FSequencerViewportFixture F;
	const FVector SavedLocation(-340, 685, 136);
	const FRotator SavedRotation(4, -15, -19);
	F.View->SetViewLocation(SavedLocation); F.View->SetViewRotation(SavedRotation);
	F.Open(); // Actual module OnPreSequencerInit callback, before native capture.
	TestEqual(TEXT("Opening CCS sequence repairs persisted Roll"), F.View->GetViewRotation().Roll, 0.);
	F.EvaluateCut();
	TestTrue(TEXT("Native Camera Cut controls the isolated viewport"), F.View->IsLockedToCinematic());
	TestTrue(TEXT("Cinematic Roll remains visible"), FMath::IsNearlyEqual(F.View->GetViewRotation().Roll, 25.));
	F.Close();
	TestFalse(TEXT("Closing releases native camera lock"), F.View->IsLockedToCinematic());
	TestTrue(TEXT("Closing restores editor position"), F.View->GetViewLocation().Equals(SavedLocation));
	TestTrue(TEXT("Closing restores pitch/yaw with zero Roll"), F.View->GetViewRotation().Equals(FRotator(4, -15, 0)));
	TestTrue(TEXT("Authored camera Roll is unchanged"), FMath::IsNearlyEqual(F.Camera->GetActorRotation().Roll, 25.));
	auto& Module = FModuleManager::GetModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	TestFalse(TEXT("Retained closed Sequencer is removed from live cache"), Module.FindOpenSequencerForSequence(F.Sequence.Get()).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequencerViewportShutdownTest,
	"ComposableCameraSystem.Editor.SequencerViewport.ShutdownRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSequencerViewportShutdownTest::RunTest(const FString&)
{
	if (!CanRunViewportRegression()) return false;
	for (bool bRestore : {true, false})
	{
		FSequencerViewportFixture F;
		const FVector EditorLocation(-500, 20, 70);
		F.View->SetViewLocation(EditorLocation); F.View->SetViewRotation(FRotator(10, 15, 0));
		F.Open(bRestore); F.EvaluateCut();
		const FVector CinematicLocation = F.View->GetViewLocation();
		TestTrue(TEXT("Shutdown starts with cinematic Roll"), FMath::IsNearlyEqual(F.View->GetViewRotation().Roll, 25.));
		const TArray<TWeakPtr<ISequencer>> Sessions{F.Sequencer};
		// Call the same production handler without broadcasting a real editor shutdown.
		ComposableCameraSystem::SequencerViewport::RestoreBeforeShutdown(Sessions);
		TestFalse(TEXT("Final editor tick cannot reapply Camera Cuts"), F.Sequencer->IsPerspectiveViewportCameraCutEnabled());
		TestFalse(TEXT("Camera lock released before viewport persistence"), F.View->IsLockedToCinematic());
		TestEqual(TEXT("Saved viewport rotation will be upright"), F.View->GetViewRotation().Roll, 0.);
		TestTrue(TEXT("Native restore-position preference is respected"),
			F.View->GetViewLocation().Equals(bRestore ? EditorLocation : CinematicLocation));
		F.EvaluateCut();
		TestEqual(TEXT("Late evaluation leaves editing Roll upright"), F.View->GetViewRotation().Roll, 0.);
		F.Close();
		TestEqual(TEXT("Subsequent normal close is harmless"), F.View->GetViewRotation().Roll, 0.);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequencerViewportScopeTest,
	"ComposableCameraSystem.Editor.SequencerViewport.Scope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSequencerViewportScopeTest::RunTest(const FString&)
{
	if (!CanRunViewportRegression()) return false;
	FSequencerViewportFixture F;
	TStrongObjectPtr<ULevelSequence> Ordinary(NewObject<ULevelSequence>()); Ordinary->Initialize();
	TStrongObjectPtr<ULevelSequence> Parent(NewObject<ULevelSequence>()); Parent->Initialize();
	auto* SubTrack = Parent->GetMovieScene()->AddTrack<UMovieSceneSubTrack>();
	auto* SubSection = CastChecked<UMovieSceneSubSection>(SubTrack->CreateNewSection());
	SubSection->SetSequence(F.Sequence.Get()); SubTrack->AddSection(*SubSection);
	FSequencerInitParams Params; Params.PlaybackContext = F.World.Get();
	const auto Prepare = [&](ULevelSequence* Sequence)
	{
		F.View->SetViewRotation(FRotator(10, 20, 30)); Params.RootSequence = Sequence;
		ComposableCameraSystem::SequencerViewport::PrepareForOpen(Params);
		return F.View->GetViewRotation().Roll;
	};
	TestEqual(TEXT("Ordinary sequence does not change a viewport"), Prepare(Ordinary.Get()), 30.);
	TestEqual(TEXT("Nested CCS sequence is recognized"), Prepare(Parent.Get()), 0.);
	F.View->SetActorLock(F.Camera.Get());
	TestEqual(TEXT("Pilot lock is preserved"), Prepare(Parent.Get()), 30.); F.View->SetActorLock(nullptr);
	F.View->SetCinematicActorLock(F.Camera.Get());
	TestEqual(TEXT("Another cinematic owner is preserved"), Prepare(Parent.Get()), 30.); F.View->SetCinematicActorLock(nullptr);
	F.View->SetAllowCinematicControl(false);
	TestEqual(TEXT("Opted-out view is preserved"), Prepare(Parent.Get()), 30.); F.View->SetAllowCinematicControl(true);
	F.View->SetViewportType(LVT_OrthoXY);
	TestEqual(TEXT("Orthographic view is preserved"), Prepare(Parent.Get()), 30.); F.View->SetViewportType(LVT_Perspective);
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> Kind(F.World->WorldType, EWorldType::PIE);
		TestEqual(TEXT("PIE context is untouched"), Prepare(Parent.Get()), 30.);
	}
	TestTrue(TEXT("PIE guard restores editor world type"), F.World->WorldType == EWorldType::Editor);
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> Kind(F.World->WorldType, EWorldType::EditorPreview);
		TestEqual(TEXT("Preview context is untouched"), Prepare(Parent.Get()), 30.);
	}
	TestTrue(TEXT("Preview guard restores editor world type"), F.World->WorldType == EWorldType::Editor);
	// A second world has its own viewport; opening the first sequence must not edit it.
	FSequencerViewportFixture Other(false);
	Other.View->SetViewRotation(FRotator(0, 0, 40)); Prepare(Parent.Get());
	TestEqual(TEXT("Different editor world is untouched"), Other.View->GetViewRotation().Roll, 40.);
	Other.Open(); Other.EvaluateCut();
	const TArray<TWeakPtr<ISequencer>> OrdinarySessions{Other.Sequencer, TWeakPtr<ISequencer>()};
	ComposableCameraSystem::SequencerViewport::RestoreBeforeShutdown(OrdinarySessions);
	TestTrue(TEXT("Shutdown guard leaves ordinary Camera Cuts enabled"), Other.Sequencer->IsPerspectiveViewportCameraCutEnabled());
	TestTrue(TEXT("Shutdown guard leaves ordinary cinematic ownership intact"), Other.View->IsLockedToCinematic());
	TestTrue(TEXT("Ordinary cinematic Roll is preserved"), FMath::IsNearlyEqual(Other.View->GetViewRotation().Roll, 25.));
	// New Camera Instance sections need the same policy as legacy Shot sections.
	UMovieSceneTrack* Instances = Ordinary->GetMovieScene()->AddTrack<UMovieSceneComposableCameraInstanceTrack>();
	UMovieSceneSection* Instance = Instances->CreateNewSection(); Instances->AddSection(*Instance);
	TestEqual(TEXT("Camera Instance is recognized"), Prepare(Ordinary.Get()), 0.);
	return true;
}
#endif
