// Copyright 2026 Sulley. All Rights Reserved.

#include "Math/ComposableCameraLockOnAimPoint.h"
#include "Nodes/ComposableCameraLockOnAimPointNode.h"

#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraLockOnAimPointNodeTests"

namespace
{
	const FVector2D DefaultPitchRange(-45.f, 45.f);

	void AddLockOnAimPointVectorOutputSlot(FComposableCameraRuntimeDataBlock& RuntimeData, int32 NodeIndex, FName PinName, int32 Offset)
	{
		RuntimeData.Storage.SetNumZeroed(Offset + static_cast<int32>(sizeof(FVector)));
		RuntimeData.TotalSize = RuntimeData.Storage.Num();
		RuntimeData.SlotShapes.Add(Offset, {
			EComposableCameraPinType::Vector3D,
			static_cast<int32>(sizeof(FVector)),
			nullptr
		});
		RuntimeData.OutputPinOffsets.Add(FComposableCameraPinKey{NodeIndex, PinName}, Offset);
	}

	AActor* SpawnLockOnAimPointActorWithRoot(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity) : nullptr;
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointOutsideRadiusReturnsRawAimTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.OutsideRadiusReturnsRawAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointOutsideRadiusReturnsRawAimTest::RunTest(const FString& Parameters)
{
	FComposableCameraLockOnAimPointState State;
	State.bInModify = true;

	const FVector Result = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector(1000.f, 0.f, 100.f),
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(0.2f, 0.5f, 0.3f),
		DefaultPitchRange,
		State);

	UTEST_TRUE("Raw aim is preserved outside radius",
		Result.Equals(FVector(1000.f, 0.f, 100.f), KINDA_SMALL_NUMBER));
	UTEST_FALSE("Correction state clears outside radius", State.bInModify);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointPitchAdditionEnforcesPlanarRadiusTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.PitchAdditionEnforcesPlanarRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointPitchAdditionEnforcesPlanarRadiusTest::RunTest(const FString& Parameters)
{
	FComposableCameraLockOnAimPointState State;

	const FVector Result = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector(100.f, 0.f, 100.f),
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		DefaultPitchRange,
		State);

	UTEST_TRUE("Correction state enters inside radius", State.bInModify);
	UTEST_TRUE("Planar distance is corrected to radius",
		FMath::IsNearlyEqual(FVector::Dist2D(FVector::ZeroVector, Result), 500.f, 0.01f));
	UTEST_TRUE("Current in-range pitch controls output height",
		FMath::IsNearlyEqual(Result.Z, 500.f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointUsesCurrentPitchInsideRangeTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.UsesCurrentPitchInsideRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointUsesCurrentPitchInsideRangeTest::RunTest(const FString& Parameters)
{
	FComposableCameraLockOnAimPointState State;

	ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector(100.f, 0.f, 100.f),
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		FVector2D(-60.f, 60.f),
		State);

	const FVector ResultAfterAimFalls = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector(100.f, 0.f, 0.f),
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		FVector2D(-60.f, 60.f),
		State);

	UTEST_TRUE("PitchAddition uses the current in-range pitch instead of stale entry pitch",
		ResultAfterAimFalls.Equals(FVector(500.f, 0.f, 0.f), 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointClampsPitchOutsideRangeTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.ClampsPitchOutsideRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointClampsPitchOutsideRangeTest::RunTest(const FString& Parameters)
{
	FComposableCameraLockOnAimPointState State;

	const FVector Result = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector(100.f, 0.f, 1000.f),
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		FVector2D(-45.f, 45.f),
		State);

	UTEST_TRUE("PitchAddition clamps high current pitch to the configured boundary",
		Result.Equals(FVector(500.f, 0.f, 500.f), 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointDegenerateOverlapStaysFiniteTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.DegenerateOverlapStaysFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointDegenerateOverlapStaysFiniteTest::RunTest(const FString& Parameters)
{
	FComposableCameraLockOnAimPointState State;

	const FVector Result = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector::ZeroVector,
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		DefaultPitchRange,
		State);

	UTEST_FALSE("Result has no NaN", Result.ContainsNaN());
	UTEST_TRUE("Degenerate case uses camera-forward fallback",
		FMath::IsNearlyEqual(FVector::Dist2D(FVector::ZeroVector, Result), 500.f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointOutsideRadiusBlendsOutExistingOffsetTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.OutsideRadiusBlendsOutExistingOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointOutsideRadiusBlendsOutExistingOffsetTest::RunTest(const FString& Parameters)
{
	FComposableCameraLockOnAimPointState State;

	const FVector InsideResult = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		FVector(100.f, 0.f, 100.f),
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		DefaultPitchRange,
		State,
		0.016f,
		0.5f);

	const FVector OutsideRawAim(600.f, 0.f, 100.f);
	const FVector OutsideResult = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		OutsideRawAim,
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		DefaultPitchRange,
		State,
		0.1f,
		0.5f);

	UTEST_TRUE("Inside frame still applies full correction",
		InsideResult.Equals(FVector(500.f, 0.f, 500.f), 0.01f));
	UTEST_FALSE("Leaving radius does not snap directly to raw aim",
		OutsideResult.Equals(OutsideRawAim, 0.01f));
	UTEST_TRUE("Existing correction blends toward zero",
		OutsideResult.Equals(FVector(920.f, 0.f, 420.f), 0.01f));
	UTEST_FALSE("Correction state exits while blend-out is still visible", State.bInModify);
	UTEST_TRUE("Blend-out keeps current addition until it finishes", State.bHasCurrentAddition);

	const FVector FinishedResult = ComposableCameraSystem::ComputeLockOnAimPoint(
		FVector::ZeroVector,
		OutsideRawAim,
		FVector(-300.f, 0.f, 0.f),
		FVector::ForwardVector,
		500.f,
		FVector(1.f, 0.f, 0.f),
		DefaultPitchRange,
		State,
		0.4f,
		0.5f);

	UTEST_TRUE("Blend-out finishes at the raw aim after the configured duration",
		FinishedResult.Equals(OutsideRawAim, 0.01f));
	UTEST_FALSE("Finished blend-out clears current addition", State.bHasCurrentAddition);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointNodeWritesStablePivotOutputTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.NodeWritesStablePivotOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointNodeWritesStablePivotOutputTest::RunTest(const FString& Parameters)
{
	FComposableCameraRuntimeDataBlock RuntimeData;
	AddLockOnAimPointVectorOutputSlot(RuntimeData, 0, TEXT("PivotPosition"), 0);

	UComposableCameraLockOnAimPointNode* Node = NewObject<UComposableCameraLockOnAimPointNode>();
	Node->FollowSource = EComposableCameraLockOnAimPointSource::WorldPosition;
	Node->FollowWorldPosition = FVector::ZeroVector;
	Node->AimSource = EComposableCameraLockOnAimPointSource::WorldPosition;
	Node->AimWorldPosition = FVector(100.f, 0.f, 100.f);
	Node->Radius = 500.f;
	Node->Weights = FVector(1.f, 0.f, 0.f);
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose Pose;
	Pose.Position = FVector(-300.f, 0.f, 0.f);
	Pose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = Pose;
	Node->TickNode(0.016f, Pose, OutPose);

	const FVector Result = RuntimeData.ReadOutputPin<FVector>(0, TEXT("PivotPosition"));

	UTEST_TRUE("Node writes the stable aim point to PivotPosition",
		Result.Equals(FVector(500.f, 0.f, 500.f), 0.01f));
	UTEST_TRUE("Node does not mutate camera pose",
		OutPose.Position.Equals(Pose.Position, KINDA_SMALL_NUMBER)
		&& OutPose.Rotation.Equals(Pose.Rotation, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLockOnAimPointNodeResolvesActorSourcesTest,
	"System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.NodeResolvesActorSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLockOnAimPointNodeResolvesActorSourcesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* FollowActor = SpawnLockOnAimPointActorWithRoot(World, FVector::ZeroVector);
	AActor* AimActor = SpawnLockOnAimPointActorWithRoot(World, FVector(100.f, 0.f, 0.f));

	FComposableCameraRuntimeDataBlock RuntimeData;
	AddLockOnAimPointVectorOutputSlot(RuntimeData, 0, TEXT("PivotPosition"), 0);

	UComposableCameraLockOnAimPointNode* Node = NewObject<UComposableCameraLockOnAimPointNode>();
	Node->FollowSource = EComposableCameraLockOnAimPointSource::ActorPosition;
	Node->FollowActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->FollowActor = FollowActor;
	Node->AimSource = EComposableCameraLockOnAimPointSource::ActorPosition;
	Node->AimActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->AimActor = AimActor;
	Node->AimWorldUpOffset = 100.f;
	Node->Radius = 500.f;
	Node->Weights = FVector(1.f, 0.f, 0.f);
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose Pose;
	Pose.Position = FVector(-300.f, 0.f, 0.f);
	Pose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = Pose;
	Node->TickNode(0.016f, Pose, OutPose);

	const FVector Result = RuntimeData.ReadOutputPin<FVector>(0, TEXT("PivotPosition"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Node resolves follow and aim through actor sources",
		Result.Equals(FVector(500.f, 0.f, 500.f), 0.01f));

	return true;
}

#undef LOCTEXT_NAMESPACE
