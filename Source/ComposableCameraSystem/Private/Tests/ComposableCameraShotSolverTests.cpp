// Copyright Sulley. All rights reserved.

// Unit tests for the Composition Solver — three-layer pipeline
// (Placement / Aim / Lens) plus independent Focus and Roll composition.
// See Docs/ShotBasedKeyframing.md §3-4.
//
// Tests that need an AActor (SingleTarget anchor, InheritFromActor basis)
// spin up a minimal Game-typed world. Pure math tests (anchor arithmetic,
// position / rotation / FOV / focus math) call the solver functions
// directly with FixedWorldPosition anchors and pre-computed inputs — no
// world needed.

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "DataAssets/ComposableCameraTargetInfo.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/ComposableCameraShotSolver.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotSolverTests"

namespace ComposableCameraShotSolverTest
{
	/** Minimal test world for spawning bare AActor instances at known
	 *  transforms — used by tests that need ResolveWorldPoint / basis
	 *  resolution to dereference a real actor. */
	struct FShotTestWorld
	{
		UWorld* World{ nullptr };

		FShotTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FShotTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AActor* SpawnActorAt(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			// Bare AActor::StaticClass() spawns WITHOUT a RootComponent —
			// GetActorLocation/GetActorQuat then always return identity, and
			// any Transform passed to SpawnActor is silently ignored. We add
			// a USceneComponent as the root and set the transform after the
			// fact so test setup actually works.
			AActor* Actor = World->SpawnActor<AActor>(
				AActor::StaticClass(), FTransform::Identity);
			if (!Actor)
			{
				return nullptr;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocationAndRotation(Location, Rotation);
			return Actor;
		}
	};

	/** Build a SingleTarget anchor pointing at index N. */
	inline FComposableCameraAnchorSpec MakeSingleTargetAnchor(int32 Index = 0)
	{
		FComposableCameraAnchorSpec A;
		A.Mode = EShotAnchorMode::SingleTarget;
		A.TargetIndex = Index;
		return A;
	}

	/** Build a FixedWorldPosition anchor at the given world point. */
	inline FComposableCameraAnchorSpec MakeFixedAnchor(const FVector& WorldPos)
	{
		FComposableCameraAnchorSpec A;
		A.Mode = EShotAnchorMode::FixedWorldPosition;
		A.WorldPosition = WorldPos;
		return A;
	}

	/** Build a ShotTarget pointing at an actor with no bone / no offset. */
	inline FComposableCameraShotTarget MakeBasicTarget(AActor* Actor)
	{
		FComposableCameraShotTarget T;
		T.Target.Actor = Actor;
		return T;
	}
}

// ============================================================================
// FComposableCameraAnchorSpec::ResolveWorldPosition (3 modes)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotAnchorFixedWorldPositionTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorFixedWorldPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotAnchorFixedWorldPositionTest::RunTest(const FString&)
{
	const FComposableCameraAnchorSpec A = ComposableCameraShotSolverTest::MakeFixedAnchor(
		FVector(100, 200, 300));

	FVector OutPos = FVector::ZeroVector;
	UTEST_TRUE("FixedWorldPosition resolves", A.ResolveWorldPosition({}, OutPos));
	UTEST_TRUE("FixedWorldPosition returns authored point",
		OutPos.Equals(FVector(100, 200, 300), 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotAnchorSingleTargetTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorSingleTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotAnchorSingleTargetTest::RunTest(const FString&)
{
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;
	AActor* A = TestWorld.SpawnActorAt(FVector(50, 60, 70));

	TArray<FComposableCameraShotTarget> Targets;
	Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(A));

	{
		const auto Anchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
		FVector Out = FVector::ZeroVector;
		UTEST_TRUE("SingleTarget valid index resolves", Anchor.ResolveWorldPosition(Targets, Out));
		UTEST_TRUE("SingleTarget returns actor location", Out.Equals(FVector(50, 60, 70), 1e-3));
	}
	{
		const auto Anchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(5);
		FVector Out = FVector(999, 999, 999);
		UTEST_FALSE("SingleTarget invalid index returns false",
			Anchor.ResolveWorldPosition(Targets, Out));
		UTEST_TRUE("OutPos unchanged on failure", Out.Equals(FVector(999, 999, 999), 1e-3));
	}
	{
		TArray<FComposableCameraShotTarget> NullTargets;
		NullTargets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(nullptr));
		const auto Anchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
		FVector Out = FVector::ZeroVector;
		UTEST_FALSE("SingleTarget null actor returns false",
			Anchor.ResolveWorldPosition(NullTargets, Out));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotAnchorWeightedCentroidTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorWeightedCentroid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotAnchorWeightedCentroidTest::RunTest(const FString&)
{
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;
	AActor* A = TestWorld.SpawnActorAt(FVector(0, 0, 0));
	AActor* B = TestWorld.SpawnActorAt(FVector(100, 0, 0));

	TArray<FComposableCameraShotTarget> Targets;
	Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(A));
	Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(B));

	{
		FComposableCameraAnchorSpec Anchor;
		Anchor.Mode = EShotAnchorMode::WeightedWorldCentroid;
		Anchor.WeightedTargets.Add({ 0, 1.f });
		Anchor.WeightedTargets.Add({ 1, 1.f });

		FVector Out = FVector::ZeroVector;
		UTEST_TRUE("Weighted centroid resolves", Anchor.ResolveWorldPosition(Targets, Out));
		UTEST_TRUE("Equal weights → midpoint", Out.Equals(FVector(50, 0, 0), 1e-3));
	}
	{
		FComposableCameraAnchorSpec Anchor;
		Anchor.Mode = EShotAnchorMode::WeightedWorldCentroid;
		Anchor.WeightedTargets.Add({ 0, 1.f });
		Anchor.WeightedTargets.Add({ 1, 3.f });

		FVector Out = FVector::ZeroVector;
		UTEST_TRUE("Asymmetric weighted centroid resolves",
			Anchor.ResolveWorldPosition(Targets, Out));
		UTEST_TRUE("3:1 weight → 75% toward B", Out.Equals(FVector(75, 0, 0), 1e-3));
	}
	{
		FComposableCameraAnchorSpec Anchor;
		Anchor.Mode = EShotAnchorMode::WeightedWorldCentroid;
		// Empty weights → no contribution.
		FVector Out(999, 999, 999);
		UTEST_FALSE("Empty weighted centroid returns false",
			Anchor.ResolveWorldPosition(Targets, Out));
	}
	return true;
}

// ============================================================================
// ResolvePlacementBasis (Layer 1 helper)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotPlacementBasisTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.PlacementBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotPlacementBasisTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// World basis → identity quat.
	{
		FComposableCameraShot Shot;
		Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
		const FQuat Q = ResolvePlacementBasis(Shot);
		UTEST_TRUE("World basis = identity", Q.Equals(FQuat::Identity, 1e-4));
	}
	// InheritFromActor with valid index → actor's quat.
	{
		AActor* A = TestWorld.SpawnActorAt(FVector::ZeroVector, FRotator(0, 90, 0));
		FComposableCameraShot Shot;
		Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(A));
		Shot.Placement.BasisFrame = EShotPlacementBasisFrame::InheritFromActor;
		Shot.Placement.BasisActorIndex = 0;
		const FQuat Q = ResolvePlacementBasis(Shot);
		UTEST_TRUE("InheritFromActor uses actor quat",
			Q.Equals(FRotator(0, 90, 0).Quaternion(), 1e-3));
	}
	// InheritFromActor with out-of-range index → identity fallback (warning).
	{
		FComposableCameraShot Shot;
		Shot.Placement.BasisFrame = EShotPlacementBasisFrame::InheritFromActor;
		Shot.Placement.BasisActorIndex = 5;
		const FQuat Q = ResolvePlacementBasis(Shot);
		UTEST_TRUE("Out-of-range index → identity fallback",
			Q.Equals(FQuat::Identity, 1e-4));
	}
	return true;
}

// ============================================================================
// SolveAnchorOrbitPosition (Layer 1 — Position math)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotAnchorOrbitPositionTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorOrbitPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotAnchorOrbitPositionTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;

	const FVector AnchorPos(0, 0, 0);
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));   // FOV=90°
	const float Aspect = 1.f;

	// Yaw=0, Pitch=0, Distance=200, no screen offset → camera at (200, 0, 0).
	{
		const FVector CamPos = SolveAnchorOrbitPosition(
			AnchorPos, FQuat::Identity, 200.f,
			FVector2D(0.f, 0.f), FVector2D::ZeroVector,
			TanHalfHOR, Aspect);
		UTEST_TRUE("Yaw 0, Pitch 0 → (+X, 0, 0)",
			CamPos.Equals(FVector(200, 0, 0), 1e-2));
	}
	// Yaw=180 → camera behind anchor along -X.
	{
		const FVector CamPos = SolveAnchorOrbitPosition(
			AnchorPos, FQuat::Identity, 200.f,
			FVector2D(180.f, 0.f), FVector2D::ZeroVector,
			TanHalfHOR, Aspect);
		UTEST_TRUE("Yaw 180 → (-X, 0, 0)",
			CamPos.Equals(FVector(-200, 0, 0), 1e-2));
	}
	// Yaw=90 → camera at +Y side of anchor.
	{
		const FVector CamPos = SolveAnchorOrbitPosition(
			AnchorPos, FQuat::Identity, 200.f,
			FVector2D(90.f, 0.f), FVector2D::ZeroVector,
			TanHalfHOR, Aspect);
		UTEST_TRUE("Yaw 90 → (0, +Y, 0)",
			CamPos.Equals(FVector(0, 200, 0), 1e-2));
	}
	// ScreenPosition (0.2, 0) — UE LHS:
	//   Forward_t = -DirWorld = -(1,0,0) = (-1,0,0)
	//   CamRight  = up × forward = (0,0,1) × (-1,0,0) = (0, -1, 0)
	//   DRight    = -ScreenPos.X · 2·TanH · Distance = -0.2·2·1·200 = -80
	//   CamPos   += DRight · CamRight = -80 · (0,-1,0) = (0, 80, 0)
	// Final: (200, 0, 0) + (0, 80, 0) = (200, 80, 0).
	// The shift moves the camera to +Y in WORLD, which is the camera's
	// own LEFT (forward=-X, right=-Y) → anchor relative to camera moves
	// to camera's RIGHT → screen X positive → matches authored ScreenPos.X=+0.2.
	{
		const FVector CamPos = SolveAnchorOrbitPosition(
			AnchorPos, FQuat::Identity, 200.f,
			FVector2D(0.f, 0.f), FVector2D(0.2f, 0.f),
			TanHalfHOR, Aspect);
		UTEST_TRUE("ScreenPosition (0.2, 0) shifts CamPos to camera's left (+Y world)",
			CamPos.Equals(FVector(200, 80, 0), 1e-2));
	}
	return true;
}

// ============================================================================
// SolveLookAtAnchorRotation (Layer 2 — Rotation math)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotLookAtAnchorRotationTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.LookAtAnchorRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotLookAtAnchorRotationTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;

	const FVector CamPos(200, 0, 0);
	const FVector AnchorPos(0, 0, 0);
	const float FOV = 90.f;
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
	const float Aspect = 1.f;

	// Aim at center → camera looks back at anchor (yaw 180).
	{
		const FRotator R = SolveLookAtAnchorRotation(
			CamPos, AnchorPos, FVector2D(0.f, 0.f),
			0.f, TanHalfHOR, Aspect);
		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			AnchorPos, CamPos, R, TanHalfHOR, Aspect, Projected);
		UTEST_TRUE("Anchor projects in front", bOk);
		UTEST_TRUE("Anchor at screen center",
			Projected.Equals(FVector2D::ZeroVector, 1e-3));
	}
	// Off-center aim → anchor projects to that screen position.
	{
		const FVector2D ScreenTarget(0.25f, -0.1f);
		const FRotator R = SolveLookAtAnchorRotation(
			CamPos, AnchorPos, ScreenTarget,
			0.f, TanHalfHOR, Aspect);
		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			AnchorPos, CamPos, R, TanHalfHOR, Aspect, Projected);
		UTEST_TRUE("Anchor projects in front", bOk);
		UTEST_TRUE("Anchor lands at screen target",
			Projected.Equals(ScreenTarget, 1e-3));
	}
	return true;
}

// ============================================================================
// SolvePlacement (Layer 1 dispatch — AnchorOrbit + FixedWorldPosition)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolvePlacementTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.SolvePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolvePlacementTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));
	const float Aspect = 1.f;

	// AnchorOrbit (pure spherical, default): no lateral shift even when
	// ScreenPosition is non-zero — solver ignores the field for this mode.
	{
		FComposableCameraShot Shot;
		Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
		Shot.Placement.PlacementAnchor =
			ComposableCameraShotSolverTest::MakeFixedAnchor(FVector::ZeroVector);
		Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
		Shot.Placement.Distance = 200.f;
		Shot.Placement.LocalCameraDirection = FVector2D(0.f, 0.f);
		Shot.Placement.ScreenPosition = FVector2D(0.3f, 0.0f);   // ignored

		FVector CamPos;
		UTEST_TRUE("AnchorOrbit resolves", SolvePlacement(Shot, TanHalfHOR, Aspect, CamPos));
		UTEST_TRUE("AnchorOrbit ignores ScreenPosition",
			CamPos.Equals(FVector(200, 0, 0), 1e-2));
	}
	// AnchorAtScreen is a joint Position+Rotation solve that
	// requires Aim data alongside Placement — exercised via SolveShot in
	// FShotSolveAnchorAtScreenTest below; SolvePlacement for
	// this mode is a deliberate ensure-fail (the orchestrator routes
	// around it).
	// FixedWorldPosition mode.
	{
		FComposableCameraShot Shot;
		Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
		Shot.Placement.FixedWorldPosition = FVector(1234, 5678, 90);

		FVector CamPos;
		UTEST_TRUE("FixedWorldPosition resolves", SolvePlacement(Shot, TanHalfHOR, Aspect, CamPos));
		UTEST_TRUE("FixedWorldPosition camera at authored point",
			CamPos.Equals(FVector(1234, 5678, 90), 1e-2));
	}
	// AnchorOrbit with unresolvable placement anchor → false.
	{
		FComposableCameraShot Shot;
		Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
		Shot.Placement.PlacementAnchor =
			ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
		// Targets is empty — index 0 invalid.
		FVector CamPos = FVector(999, 999, 999);
		UTEST_FALSE("Unresolvable anchor → false", SolvePlacement(Shot, TanHalfHOR, Aspect, CamPos));
	}
	return true;
}

// ============================================================================
// SolveAim (Layer 2 dispatch + Roll composition)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveAimTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.SolveAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveAimTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;

	const FVector CamPos(200, 0, 0);
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));
	const float Aspect = 1.f;

	FComposableCameraShot Shot;
	Shot.Aim.AimAnchor =
		ComposableCameraShotSolverTest::MakeFixedAnchor(FVector::ZeroVector);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.f, 0.f);

	{
		Shot.Roll = 0.f;
		FRotator R;
		UTEST_TRUE("Aim resolves", SolveAim(Shot, CamPos, TanHalfHOR, Aspect, R));
		UTEST_TRUE("Roll = 0", FMath::IsNearlyEqual(R.Roll, 0.f, 1e-3));
	}
	{
		Shot.Roll = 25.f;
		FRotator R;
		UTEST_TRUE("Aim resolves with Roll", SolveAim(Shot, CamPos, TanHalfHOR, Aspect, R));
		UTEST_TRUE("Output Roll = 25", FMath::IsNearlyEqual(R.Roll, 25.f, 1e-3));
	}
	// NoOp short-circuits BEFORE AimAnchor resolution — invalid AimAnchor
	// must not produce a "unresolvable" failure when NoOp is active.
	{
		Shot.Aim.Mode = EShotAimMode::NoOp;
		Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(99);
		Shot.Roll = 0.f;
		FRotator R(99, 99, 99);   // pre-seed to detect that NoOp writes
		UTEST_TRUE("NoOp resolves regardless of AimAnchor",
			SolveAim(Shot, CamPos, TanHalfHOR, Aspect, R));
		UTEST_TRUE("NoOp produces identity rotation",
			R.Equals(FRotator::ZeroRotator, 1e-3));
	}
	// NoOp still composes Roll.
	{
		Shot.Aim.Mode = EShotAimMode::NoOp;
		Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeFixedAnchor(FVector::ZeroVector);
		Shot.Roll = 30.f;
		FRotator R;
		UTEST_TRUE("NoOp + Roll resolves", SolveAim(Shot, CamPos, TanHalfHOR, Aspect, R));
		UTEST_TRUE("NoOp + Roll → (0, 0, 30)",
			R.Equals(FRotator(0.f, 0.f, 30.f), 1e-3));
	}
	return true;
}

// ============================================================================
// SolveFocus (4 modes + fallback)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveFocusTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.SolveFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveFocusTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	AActor* A = TestWorld.SpawnActorAt(FVector(0, 0, 0));
	AActor* B = TestWorld.SpawnActorAt(FVector(50, 0, 0));

	const FVector CamPos(200, 0, 0);
	const FRotator CamRot(0, 180, 0);   // looks at -X

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(A));
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(B));
	Shot.Placement.PlacementAnchor =
		ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Aim.AimAnchor =
		ComposableCameraShotSolverTest::MakeSingleTargetAnchor(1);
	Shot.Focus.ManualDistance = 425.f;

	// Manual mode.
	{
		Shot.Focus.Mode = EShotFocusMode::Manual;
		const float D = SolveFocus(Shot, CamPos, CamRot);
		UTEST_TRUE("Manual returns ManualDistance",
			FMath::IsNearlyEqual(D, 425.f, 0.5f));
	}
	// FollowPlacementAnchor → distance to A (at origin) = 200.
	{
		Shot.Focus.Mode = EShotFocusMode::FollowPlacementAnchor;
		const float D = SolveFocus(Shot, CamPos, CamRot);
		UTEST_TRUE("FollowPlacementAnchor → 200",
			FMath::IsNearlyEqual(D, 200.f, 0.5f));
	}
	// FollowAimAnchor → distance to B (at 50,0,0) = 150.
	{
		Shot.Focus.Mode = EShotFocusMode::FollowAimAnchor;
		const float D = SolveFocus(Shot, CamPos, CamRot);
		UTEST_TRUE("FollowAimAnchor → 150",
			FMath::IsNearlyEqual(D, 150.f, 0.5f));
	}
	// FollowCustomAnchor with a fixed point at (100, 0, 0) → 100.
	{
		Shot.Focus.Mode = EShotFocusMode::FollowCustomAnchor;
		Shot.Focus.FocusAnchor =
			ComposableCameraShotSolverTest::MakeFixedAnchor(FVector(100, 0, 0));
		const float D = SolveFocus(Shot, CamPos, CamRot);
		UTEST_TRUE("FollowCustomAnchor → 100",
			FMath::IsNearlyEqual(D, 100.f, 0.5f));
	}
	// FollowCustomAnchor with unresolvable anchor → fallback to ManualDistance.
	{
		Shot.Focus.Mode = EShotFocusMode::FollowCustomAnchor;
		Shot.Focus.FocusAnchor =
			ComposableCameraShotSolverTest::MakeSingleTargetAnchor(99);
		const float D = SolveFocus(Shot, CamPos, CamRot);
		UTEST_TRUE("Unresolvable Custom anchor → ManualDistance fallback",
			FMath::IsNearlyEqual(D, 425.f, 0.5f));
	}
	return true;
}

// ============================================================================
// SolveShot end-to-end — single anchor + dual anchor (OTS-style)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveSingleAnchorTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.SolveShotSingleAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveSingleAnchorTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// PlacementAnchor == AimAnchor (== Hero at origin). Camera at distance
	// 200 looking back at Hero, both ScreenPositions = (0, 0).
	AActor* A = TestWorld.SpawnActorAt(FVector::ZeroVector);

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(A));
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Shot.Placement.Distance = 200.f;
	Shot.Placement.LocalCameraDirection = FVector2D(0.f, 0.f);
	Shot.Placement.ScreenPosition = FVector2D(0.f, 0.f);
	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.f, 0.f);
	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;
	Shot.Lens.Aperture = 2.8f;
	Shot.Focus.Mode = EShotFocusMode::FollowAimAnchor;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 1.f;
	Ctx.PreviousFrameFOV = 90.f;

	const FShotSolveResult R = SolveShot(Shot, Ctx);

	UTEST_TRUE("Solve succeeded", R.bValid);
	UTEST_TRUE("Camera at +X 200 from anchor",
		R.CameraPosition.Equals(FVector(200, 0, 0), 1e-2));
	UTEST_TRUE("Camera yaw ≈ 180 (looking back at anchor)",
		FMath::IsNearlyEqual(FMath::Abs(R.CameraRotation.Yaw), 180.f, 0.5f));
	UTEST_TRUE("FOV = 90", FMath::IsNearlyEqual(R.FieldOfView, 90.f, 1e-3));
	UTEST_TRUE("Aperture = 2.8", FMath::IsNearlyEqual(R.Aperture, 2.8f, 1e-3));
	UTEST_TRUE("FocusDistance = 200 (FollowAimAnchor)",
		FMath::IsNearlyEqual(R.FocusDistance, 200.f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveDualAnchorTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.SolveShotDualAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveDualAnchorTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// PlacementAnchor = Hero (at origin), AimAnchor = Villain (at +400 X).
	// Camera placed 200 behind Hero (yaw 180 in world basis) → CamPos at
	// (-200, 0, 0). Camera looks at Villain → faces +X. Villain projects at
	// screen (0, 0). Hero (between camera and Villain) also projects in
	// front of camera at center (since Hero is on the camera→Villain line,
	// just closer).
	AActor* Hero    = TestWorld.SpawnActorAt(FVector::ZeroVector);
	AActor* Villain = TestWorld.SpawnActorAt(FVector(400, 0, 0));

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Hero));
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Villain));

	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Shot.Placement.Distance = 200.f;
	Shot.Placement.LocalCameraDirection = FVector2D(180.f, 0.f);   // behind Hero
	Shot.Placement.ScreenPosition = FVector2D(0.f, 0.f);

	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(1);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.f, 0.f);

	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 1.f;
	Ctx.PreviousFrameFOV = 90.f;

	const FShotSolveResult R = SolveShot(Shot, Ctx);
	UTEST_TRUE("Solve succeeded", R.bValid);
	UTEST_TRUE("Camera at (-200, 0, 0)",
		R.CameraPosition.Equals(FVector(-200, 0, 0), 1e-2));

	// Verify Villain (AimAnchor) projects to screen center.
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));
	FVector2D Projected;
	const bool bOk = ProjectWorldPointToScreen(
		FVector(400, 0, 0), R.CameraPosition, R.CameraRotation,
		TanHalfHOR, 1.f, Projected);
	UTEST_TRUE("Villain projects in front", bOk);
	UTEST_TRUE("Villain at screen center",
		Projected.Equals(FVector2D::ZeroVector, 1e-3));
	return true;
}

// ============================================================================
// AnchorAtScreen — joint Aim+Placement solve (OTS)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveAnchorAtScreenTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorAtScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveAnchorAtScreenTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// OTS-style setup:
	//   Targets[0] = Hero    at (0, 0, 0)         (PlacementAnchor — foreground)
	//   Targets[1] = Villain at (1000, 0, 0)      (AimAnchor — distant subject)
	//   Aim.ScreenPosition       = (0, 0)           (Villain centered)
	//   Placement.ScreenPosition = (-0.25, 0)        (Hero on left third)
	//   Placement.Distance       = 100              (Hero ~1m in front of camera)
	//
	// Joint solve places camera such that Hero is at depth 100 and screen
	// (-0.25, 0), while Villain projects to screen center.
	AActor* Hero    = TestWorld.SpawnActorAt(FVector::ZeroVector);
	AActor* Villain = TestWorld.SpawnActorAt(FVector(1000, 0, 0));

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Hero));
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Villain));

	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Distance = 100.f;
	Shot.Placement.ScreenPosition = FVector2D(-0.25f, 0.f);

	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(1);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.f, 0.f);

	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 1.f;
	Ctx.PreviousFrameFOV = 90.f;

	const FShotSolveResult R = SolveShot(Shot, Ctx);
	UTEST_TRUE("Joint solve succeeded", R.bValid);

	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));

	// Verify Villain (AimAnchor) projects to (0, 0).
	{
		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			FVector(1000, 0, 0), R.CameraPosition, R.CameraRotation,
			TanHalfHOR, 1.f, Projected);
		UTEST_TRUE("AimAnchor (Villain) in front of camera", bOk);
		UTEST_TRUE(*FString::Printf(TEXT("AimAnchor at (0, 0); got (%.4f, %.4f)"),
			Projected.X, Projected.Y),
			Projected.Equals(FVector2D::ZeroVector, 5e-3));
	}
	// Verify Hero (PlacementAnchor) projects to (-0.25, 0).
	{
		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			FVector::ZeroVector, R.CameraPosition, R.CameraRotation,
			TanHalfHOR, 1.f, Projected);
		UTEST_TRUE("PlacementAnchor (Hero) in front of camera", bOk);
		UTEST_TRUE(*FString::Printf(TEXT("PlacementAnchor at (-0.25, 0); got (%.4f, %.4f)"),
			Projected.X, Projected.Y),
			Projected.Equals(FVector2D(-0.25f, 0.f), 5e-3));
	}
	// Verify Hero's depth from camera = Distance (100).
	{
		const FVector Forward = R.CameraRotation.Vector();
		const float Depth = static_cast<float>(
			FVector::DotProduct(FVector::ZeroVector - R.CameraPosition, Forward));
		UTEST_TRUE(*FString::Printf(TEXT("Hero depth ≈ Distance=100 (got %.3f)"), Depth),
			FMath::IsNearlyEqual(Depth, 100.f, 0.5f));
	}
	return true;
}

// ============================================================================
// AnchorAtScreen + Aim NoOp — direct algebraic solve
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveAnchorAtScreenWithNoOpAimTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorAtScreenWithNoOpAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveAnchorAtScreenWithNoOpAimTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// Single target placement with Aim NoOp → camera rotation = identity
	// (Roll = 0 for this test). Camera placed such that Hero lands at
	// screen (0.2, -0.1) at depth 250.
	AActor* Hero = TestWorld.SpawnActorAt(FVector(500, 0, 0));

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Hero));

	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Distance = 250.f;
	Shot.Placement.ScreenPosition = FVector2D(0.2f, -0.1f);

	// Aim NoOp — AimAnchor unread; AimScreenPos unread.
	Shot.Aim.Mode = EShotAimMode::NoOp;
	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);   // ignored

	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 1.f;
	Ctx.PreviousFrameFOV = 90.f;

	const FShotSolveResult R = SolveShot(Shot, Ctx);
	UTEST_TRUE("Solve succeeded with NoOp + AnchorAtScreen", R.bValid);
	UTEST_TRUE("Camera rotation = identity (Pitch/Yaw/Roll all 0)",
		R.CameraRotation.Equals(FRotator::ZeroRotator, 1e-3));

	// Verify Hero projects to (0.2, -0.1) at depth 250.
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));
	FVector2D Projected;
	const bool bOk = ProjectWorldPointToScreen(
		FVector(500, 0, 0), R.CameraPosition, R.CameraRotation,
		TanHalfHOR, 1.f, Projected);
	UTEST_TRUE("Hero in front of camera", bOk);
	UTEST_TRUE(*FString::Printf(TEXT("Hero at screen (0.2, -0.1); got (%.4f, %.4f)"),
		Projected.X, Projected.Y),
		Projected.Equals(FVector2D(0.2f, -0.1f), 5e-3));

	const FVector Forward = R.CameraRotation.Vector();
	const float Depth = static_cast<float>(
		FVector::DotProduct(FVector(500, 0, 0) - R.CameraPosition, Forward));
	UTEST_TRUE(*FString::Printf(TEXT("Hero depth ≈ Distance=250 (got %.3f)"), Depth),
		FMath::IsNearlyEqual(Depth, 250.f, 0.5f));
	return true;
}

// ============================================================================
// AnchorAtScreen hardening — A.1 polish (pre-flight + clamp + damping)
// ============================================================================
//
// These tests cover the three failure modes the V2.1 hardening pass
// (`Docs/_HANDOFF_V2.md` Tier A.1) added to `SolveAnchorAtScreen`:
//
//   1. Anchor coincidence (PlacementAnchor == AimAnchor) → bValid=false.
//      Joint solve has no canonical answer; designer should switch to
//      AnchorOrbit. Replaces the previous "warn + use ForwardVector
//      fallback" silent-bad-pose behavior.
//
//   2. Authored screen positions outside the soft frustum envelope
//      (`[-0.49, +0.49]²`) → clamped, solve succeeds at the clamped
//      value. Designer sees a warning so they know their authored value
//      was modified.
//
//   3. Damped Picard converges on off-center / short-Distance geometries
//      that would have oscillated under the previous un-damped iteration
//      (smoke test: a moderately stressful OTS configuration converges
//      and lands the anchors at their authored screen positions).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveAnchorAtScreenDegenerateAnchorTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorAtScreenDegenerateAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveAnchorAtScreenDegenerateAnchorTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// Single actor — both Placement and Aim anchors point to the SAME
	// world position. Joint solve has no canonical (PlacementAnchor →
	// AimAnchor) direction; the hardened solver hard-fails here.
	AActor* Hero = TestWorld.SpawnActorAt(FVector(500, 0, 0));

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Hero));

	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Distance = 200.f;
	Shot.Placement.ScreenPosition = FVector2D(-0.25f, 0.f);

	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);   // SAME as Placement
	Shot.Aim.ScreenPosition = FVector2D(0.f, 0.f);

	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 1.f;
	Ctx.PreviousFrameFOV = 90.f;

	// Suppress the expected solver warning so the test log stays clean.
	AddExpectedError(TEXT("PlacementAnchor == AimAnchor"),
		EAutomationExpectedErrorFlags::Contains, 1);

	const FShotSolveResult R = SolveShot(Shot, Ctx);
	UTEST_FALSE("Degenerate-anchor joint solve must hard-fail (bValid=false)", R.bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveAnchorAtScreenScreenPosClampTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorAtScreenScreenPosClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveAnchorAtScreenScreenPosClampTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// OTS-style setup, but PlacementScreenPos = (-0.9, 0) — well outside
	// the soft envelope `[-0.49, +0.49]²`. The hardened solver clamps to
	// (-0.49, 0) and continues; verify the camera lands the
	// PlacementAnchor at the *clamped* screen position, not the authored
	// one.
	AActor* Hero    = TestWorld.SpawnActorAt(FVector::ZeroVector);
	AActor* Villain = TestWorld.SpawnActorAt(FVector(1000, 0, 0));

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Hero));
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Villain));

	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Distance = 100.f;
	Shot.Placement.ScreenPosition = FVector2D(-0.9f, 0.f);   // outside envelope

	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(1);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.f, 0.f);

	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 1.f;
	Ctx.PreviousFrameFOV = 90.f;

	AddExpectedError(TEXT("Placement.ScreenPosition clamped"),
		EAutomationExpectedErrorFlags::Contains, 1);

	const FShotSolveResult R = SolveShot(Shot, Ctx);
	UTEST_TRUE("Clamped solve still succeeds", R.bValid);

	// Verify Hero (PlacementAnchor) projects to the clamped X (= -0.49),
	// not the authored -0.9. Y stays 0.
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));
	FVector2D Projected;
	const bool bOk = ProjectWorldPointToScreen(
		FVector::ZeroVector, R.CameraPosition, R.CameraRotation,
		TanHalfHOR, 1.f, Projected);
	UTEST_TRUE("PlacementAnchor in front of camera", bOk);
	UTEST_TRUE(*FString::Printf(TEXT("PlacementAnchor at clamped (-0.49, 0); got (%.4f, %.4f)"),
		Projected.X, Projected.Y),
		Projected.Equals(FVector2D(-0.49f, 0.f), 5e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveAnchorAtScreenDampedConvergenceTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.AnchorAtScreenDampedConvergence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveAnchorAtScreenDampedConvergenceTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	// Stressful OTS geometry — short Distance (50cm), off-center
	// PlacementScreenPos (-0.4, +0.3) at the corner of the envelope, +
	// non-zero Roll (20°). Damped Picard should still converge inside
	// MaxIters=16 and land both anchors at their authored screen
	// positions to within 5e-3.
	AActor* Hero    = TestWorld.SpawnActorAt(FVector::ZeroVector);
	AActor* Villain = TestWorld.SpawnActorAt(FVector(800, 0, 50));

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Hero));
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(Villain));

	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Distance = 50.f;
	Shot.Placement.ScreenPosition = FVector2D(-0.4f, 0.3f);

	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(1);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.1f, -0.05f);

	Shot.Roll = 20.f;

	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 16.f / 9.f;
	Ctx.PreviousFrameFOV = 90.f;

	const FShotSolveResult R = SolveShot(Shot, Ctx);
	UTEST_TRUE("Damped Picard converges on stressful geometry", R.bValid);

	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(90.f * 0.5f));

	// Verify Hero (PlacementAnchor) lands at authored (-0.4, 0.3).
	{
		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			FVector::ZeroVector, R.CameraPosition, R.CameraRotation,
			TanHalfHOR, 16.f / 9.f, Projected);
		UTEST_TRUE("PlacementAnchor in front of camera", bOk);
		UTEST_TRUE(*FString::Printf(TEXT("PlacementAnchor at (-0.4, 0.3); got (%.4f, %.4f)"),
			Projected.X, Projected.Y),
			Projected.Equals(FVector2D(-0.4f, 0.3f), 5e-3));
	}
	// Verify Villain (AimAnchor) lands at authored (0.1, -0.05).
	{
		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			FVector(800, 0, 50), R.CameraPosition, R.CameraRotation,
			TanHalfHOR, 16.f / 9.f, Projected);
		UTEST_TRUE("AimAnchor in front of camera", bOk);
		UTEST_TRUE(*FString::Printf(TEXT("AimAnchor at (0.1, -0.05); got (%.4f, %.4f)"),
			Projected.X, Projected.Y),
			Projected.Equals(FVector2D(0.1f, -0.05f), 5e-3));
	}
	UTEST_TRUE(*FString::Printf(TEXT("Output Roll matches authored 20° (got %.3f)"),
		R.CameraRotation.Roll),
		FMath::IsNearlyEqual(R.CameraRotation.Roll, 20.f, 1e-3));
	return true;
}

// ============================================================================
// Roll preserves Aim.ScreenPosition (V1.1 invariant carryover)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShotSolveRollPreservesAnchorTest,
	"System.Engine.ComposableCameraSystem.ShotSolver.RollPreservesAnchorScreenPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShotSolveRollPreservesAnchorTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	using ComposableCameraSystem::ProjectWorldPointToScreen;
	ComposableCameraShotSolverTest::FShotTestWorld TestWorld;

	AActor* A = TestWorld.SpawnActorAt(FVector::ZeroVector);

	FComposableCameraShot Shot;
	Shot.Targets.Add(ComposableCameraShotSolverTest::MakeBasicTarget(A));
	Shot.Placement.PlacementAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::World;
	Shot.Placement.Distance = 200.f;
	Shot.Placement.LocalCameraDirection = FVector2D(0.f, 0.f);
	Shot.Placement.ScreenPosition = FVector2D(0.f, 0.f);
	Shot.Aim.AimAnchor = ComposableCameraShotSolverTest::MakeSingleTargetAnchor(0);
	Shot.Aim.Mode = EShotAimMode::LookAtAnchor;
	Shot.Aim.ScreenPosition = FVector2D(0.2f, 0.1f);
	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;

	FShotSolveContext Ctx;
	Ctx.ViewportAspectRatio = 16.f / 9.f;
	Ctx.PreviousFrameFOV = 90.f;

	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(Shot.Lens.ManualFOV * 0.5f));
	const float Rolls[] = { 0.f, 15.f, -30.f, 75.f };

	for (float RollDeg : Rolls)
	{
		Shot.Roll = RollDeg;
		const FShotSolveResult R = SolveShot(Shot, Ctx);
		UTEST_TRUE(*FString::Printf(TEXT("Solve succeeded for Roll=%.1f"), RollDeg), R.bValid);
		UTEST_TRUE(*FString::Printf(TEXT("Output rotation Roll matches for Roll=%.1f"), RollDeg),
			FMath::IsNearlyEqual(R.CameraRotation.Roll, RollDeg, 1e-3));

		FVector2D Projected;
		const bool bOk = ProjectWorldPointToScreen(
			FVector::ZeroVector, R.CameraPosition, R.CameraRotation,
			TanHalfHOR, Ctx.ViewportAspectRatio, Projected);
		UTEST_TRUE(*FString::Printf(TEXT("Aim anchor in front (Roll=%.1f)"), RollDeg), bOk);
		UTEST_TRUE(*FString::Printf(
			TEXT("Aim anchor projects to Aim.ScreenPosition (Roll=%.1f); got (%.4f,%.4f)"),
			RollDeg, Projected.X, Projected.Y),
			Projected.Equals(Shot.Aim.ScreenPosition, 5e-3));
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
