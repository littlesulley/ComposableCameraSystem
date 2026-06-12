// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraCompositionPreservingTransition.h"

#include "Components/SceneComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/ComposableCameraTestObjects.h"
#include "Transitions/ComposableCameraLinearTransition.h"

#define LOCTEXT_NAMESPACE "ComposableCameraCompositionPreservingTransitionTests"

namespace
{
	struct FCompositionPreservingTransitionTestWorld
	{
		UWorld* World { nullptr };

		FCompositionPreservingTransitionTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FCompositionPreservingTransitionTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AActor* SpawnActorAt(const FVector& Location)
		{
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			if (!Actor)
			{
				return nullptr;
			}

			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCompositionPreservingTransitionRebuildsSourceFromMovingSubjectTest,
	"System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.RebuildsSourceFromMovingSubject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompositionPreservingTransitionRebuildsSourceFromMovingSubjectTest::RunTest(const FString& Parameters)
{
	constexpr float DT = 0.25f;

	FCompositionPreservingTransitionTestWorld TestWorld;
	AActor* Subject = TestWorld.SpawnActorAt(FVector(100.f, 0.f, 0.f));
	UTEST_NOT_NULL("Subject actor spawned", Subject);

	UComposableCameraCompositionPreservingTransition* Transition =
		NewObject<UComposableCameraCompositionPreservingTransition>();
	Transition->DrivingTransition = NewObject<UComposableCameraLinearTransition>(Transition);
	Transition->SubjectActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Transition->SubjectActor = Subject;
	Transition->SetTransitionTime(1.f);

	FComposableCameraPose SourcePose;
	SourcePose.Position = FVector::ZeroVector;
	SourcePose.Rotation = FRotator::ZeroRotator;
	SourcePose.SetFieldOfViewDegrees(60.0);

	FComposableCameraPose TargetPose;
	TargetPose.Position = FVector(100.f, -200.f, 0.f);
	TargetPose.Rotation = FRotator(0.f, 90.f, 0.f);
	TargetPose.SetFieldOfViewDegrees(100.0);

	FComposableCameraTransitionInitParams InitParams;
	InitParams.CurrentSourcePose = SourcePose;
	InitParams.PreviousSourcePose = SourcePose;
	InitParams.DeltaTime = DT;
	Transition->TransitionEnabled(InitParams);
	Transition->ResetTransitionState();

	(void)Transition->Evaluate(DT, SourcePose, TargetPose);

	Subject->SetActorLocation(FVector(200.f, 0.f, 0.f));
	const FComposableCameraPose ResultPose = Transition->Evaluate(DT, SourcePose, TargetPose);

	const FRotator ExpectedDrivingRotation(0.f, 45.f, 0.f);
	const FVector CapturedSourceOffset(100.f, 0.f, 0.f);
	const FVector LiveTargetOffset = TargetPose.Rotation.UnrotateVector(
		Subject->GetActorLocation() - TargetPose.Position);
	const FVector ExpectedOffset = FMath::Lerp(CapturedSourceOffset, LiveTargetOffset, 0.5f);
	const FVector ExpectedPosition =
		Subject->GetActorLocation()
		- ExpectedDrivingRotation.RotateVector(ExpectedOffset);

	UTEST_TRUE("Rotation comes from the driving transition",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(ResultPose.Rotation.Yaw, 45.f), 0.f, 0.01f));
	UTEST_TRUE("Location uses the blended source/target subject offsets in driving rotation space",
		ResultPose.Position.Equals(ExpectedPosition, 0.01f));
	UTEST_TRUE("Non-transform fields blend from source pose to target pose",
		FMath::IsNearlyEqual(ResultPose.GetEffectiveFieldOfView(), 80.0, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCompositionPreservingTransitionConvergesToLiveTargetPoseTest,
	"System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.ConvergesToLiveTargetPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompositionPreservingTransitionConvergesToLiveTargetPoseTest::RunTest(const FString& Parameters)
{
	FCompositionPreservingTransitionTestWorld TestWorld;
	AActor* Subject = TestWorld.SpawnActorAt(FVector(100.f, 0.f, 0.f));
	UTEST_NOT_NULL("Subject actor spawned", Subject);

	UComposableCameraCompositionPreservingTransition* Transition =
		NewObject<UComposableCameraCompositionPreservingTransition>();
	Transition->DrivingTransition = NewObject<UComposableCameraLinearTransition>(Transition);
	Transition->SubjectActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Transition->SubjectActor = Subject;
	Transition->SetTransitionTime(1.f);

	FComposableCameraPose SourcePose;
	SourcePose.Position = FVector::ZeroVector;
	SourcePose.Rotation = FRotator::ZeroRotator;
	SourcePose.SetFieldOfViewDegrees(60.0);

	FComposableCameraPose TargetPose;
	TargetPose.Position = FVector(100.f, -200.f, 0.f);
	TargetPose.Rotation = FRotator(0.f, 90.f, 0.f);
	TargetPose.SetFieldOfViewDegrees(100.0);

	FComposableCameraTransitionInitParams InitParams;
	InitParams.CurrentSourcePose = SourcePose;
	InitParams.PreviousSourcePose = SourcePose;
	InitParams.DeltaTime = 0.01f;
	Transition->TransitionEnabled(InitParams);
	Transition->ResetTransitionState();

	(void)Transition->Evaluate(0.01f, SourcePose, TargetPose);

	Subject->SetActorLocation(FVector(200.f, 0.f, 0.f));
	const FComposableCameraPose ResultPose = Transition->Evaluate(0.98f, SourcePose, TargetPose);

	UTEST_TRUE("Penultimate composition-preserving pose converges toward live target pose",
		FVector::Dist(ResultPose.Position, TargetPose.Position) < 5.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCompositionPreservingTransitionResolvesControlledPawnFromOuterPCMTest,
	"System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.ResolvesControlledPawnFromOuterPCM",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompositionPreservingTransitionResolvesControlledPawnFromOuterPCMTest::RunTest(const FString& Parameters)
{
	constexpr float DT = 0.25f;

	FCompositionPreservingTransitionTestWorld TestWorld;

	APlayerController* FirstController =
		TestWorld.World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity);
	APlayerController* OwningController =
		TestWorld.World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity);
	APawn* FirstPawn =
		TestWorld.World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(500.f, 0.f, 0.f)));
	APawn* OwningPawn =
		TestWorld.World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(100.f, 0.f, 0.f)));
	UTEST_NOT_NULL("First controller spawned", FirstController);
	UTEST_NOT_NULL("Owning controller spawned", OwningController);
	UTEST_NOT_NULL("First pawn spawned", FirstPawn);
	UTEST_NOT_NULL("Owning pawn spawned", OwningPawn);

	FirstController->Possess(FirstPawn);
	OwningController->Possess(OwningPawn);

	AComposableCameraPlayerCameraManager* PCM =
		TestWorld.World->SpawnActor<AComposableCameraPlayerCameraManager>(
			AComposableCameraPlayerCameraManager::StaticClass(),
			FTransform::Identity);
	UTEST_NOT_NULL("PCM spawned", PCM);
	PCM->InitializeFor(OwningController);

	UComposableCameraCompositionPreservingTransition* Transition =
		NewObject<UComposableCameraCompositionPreservingTransition>(PCM);
	Transition->DrivingTransition = NewObject<UComposableCameraLinearTransition>(Transition);
	Transition->SubjectActorSource = EComposableCameraActorInputSource::ControllerControlledPawn;
	Transition->SetTransitionTime(1.f);

	FComposableCameraPose SourcePose;
	SourcePose.Position = FVector::ZeroVector;
	SourcePose.Rotation = FRotator::ZeroRotator;
	SourcePose.SetFieldOfViewDegrees(60.0);

	FComposableCameraPose TargetPose;
	TargetPose.Position = FVector(100.f, 0.f, 0.f);
	TargetPose.Rotation = FRotator(0.f, 90.f, 0.f);
	TargetPose.SetFieldOfViewDegrees(100.0);

	FComposableCameraTransitionInitParams InitParams;
	InitParams.CurrentSourcePose = SourcePose;
	InitParams.PreviousSourcePose = SourcePose;
	InitParams.DeltaTime = DT;
	Transition->TransitionEnabled(InitParams);
	Transition->ResetTransitionState();

	(void)Transition->Evaluate(DT, SourcePose, TargetPose);

	OwningPawn->SetActorLocation(FVector(200.f, 0.f, 0.f));
	const FComposableCameraPose ResultPose = Transition->Evaluate(DT, SourcePose, TargetPose);

	const FRotator ExpectedDrivingRotation(0.f, 45.f, 0.f);
	const FVector CapturedSourceOffset(100.f, 0.f, 0.f);
	const FVector LiveTargetOffset = TargetPose.Rotation.UnrotateVector(
		OwningPawn->GetActorLocation() - TargetPose.Position);
	const FVector ExpectedOffset = FMath::Lerp(CapturedSourceOffset, LiveTargetOffset, 0.5f);
	const FVector ExpectedPosition =
		OwningPawn->GetActorLocation()
		- ExpectedDrivingRotation.RotateVector(ExpectedOffset);

	UTEST_TRUE("ControlledPawn source resolves through the outer PCM owner",
		ResultPose.Position.Equals(ExpectedPosition, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTransitionBaseCachesOuterPCMTest,
	"System.Engine.ComposableCameraSystem.Transitions.Base.CachesOuterPCM",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTransitionBaseCachesOuterPCMTest::RunTest(const FString& Parameters)
{
	FCompositionPreservingTransitionTestWorld TestWorld;

	AComposableCameraPlayerCameraManager* PCM =
		TestWorld.World->SpawnActor<AComposableCameraPlayerCameraManager>(
			AComposableCameraPlayerCameraManager::StaticClass(),
			FTransform::Identity);
	UTEST_NOT_NULL("PCM spawned", PCM);

	UComposableCameraTestTransition* Transition = NewObject<UComposableCameraTestTransition>(PCM);
	UTEST_NOT_NULL("Transition spawned", Transition);

	FComposableCameraTransitionInitParams InitParams;
	Transition->TransitionEnabled(InitParams);

	UTEST_EQUAL("Transition caches its typed outer PCM on activation",
		Transition->GetCachedPlayerCameraManagerForTest(),
		PCM);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCompositionPreservingTransitionUsesDrivingTransitionTimeWhenWrapperTimeUnsetTest,
	"System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.UsesDrivingTransitionTimeWhenWrapperTimeUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompositionPreservingTransitionUsesDrivingTransitionTimeWhenWrapperTimeUnsetTest::RunTest(const FString& Parameters)
{
	constexpr float DT = 0.25f;

	FCompositionPreservingTransitionTestWorld TestWorld;
	AActor* Subject = TestWorld.SpawnActorAt(FVector(100.f, 0.f, 0.f));
	UTEST_NOT_NULL("Subject actor spawned", Subject);

	UComposableCameraLinearTransition* DrivingTransition =
		NewObject<UComposableCameraLinearTransition>();
	DrivingTransition->SetTransitionTime(1.f);

	UComposableCameraCompositionPreservingTransition* Transition =
		NewObject<UComposableCameraCompositionPreservingTransition>();
	Transition->DrivingTransition = DrivingTransition;
	Transition->SubjectActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Transition->SubjectActor = Subject;
	Transition->SetTransitionTime(0.f);

	FComposableCameraPose SourcePose;
	SourcePose.Position = FVector::ZeroVector;
	SourcePose.Rotation = FRotator::ZeroRotator;
	SourcePose.SetFieldOfViewDegrees(60.0);

	FComposableCameraPose TargetPose;
	TargetPose.Position = FVector(100.f, 0.f, 0.f);
	TargetPose.Rotation = FRotator(0.f, 90.f, 0.f);
	TargetPose.SetFieldOfViewDegrees(100.0);

	FComposableCameraTransitionInitParams InitParams;
	InitParams.CurrentSourcePose = SourcePose;
	InitParams.PreviousSourcePose = SourcePose;
	InitParams.DeltaTime = DT;
	Transition->TransitionEnabled(InitParams);
	Transition->ResetTransitionState();

	const FComposableCameraPose ResultPose = Transition->Evaluate(DT, SourcePose, TargetPose);

	UTEST_FALSE("Outer transition does not finish immediately when DrivingTransition has time",
		Transition->IsFinished());
	UTEST_TRUE("Outer transition adopts DrivingTransition duration",
		FMath::IsNearlyEqual(Transition->GetTransitionTime(), 1.f, KINDA_SMALL_NUMBER));
	UTEST_TRUE("First-frame output is an actual blend, not an immediate target hard cut",
		!ResultPose.Position.Equals(TargetPose.Position, 0.01f));

	return true;
}

#undef LOCTEXT_NAMESPACE
