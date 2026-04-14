// Copyright Sulley. All rights reserved.

// Tests for bugs fixed during the codebase review pass.
// BUG 4: ReactivateCurrentCamera null dereference
// BUG 6: SplineTransition Smooth vs Smoother produce different results

#include "Math/ComposableCameraMath.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ComposableCameraBugFixTests"

// ============================================================================
// Test: SmoothStep and SmootherStep produce different results (BUG 6)
// Verifies the Smooth/Smoother enum cases in SplineTransition call
// different math functions.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothStepVsSmootherStepTest,
	"System.Engine.ComposableCameraSystem.Math.SmoothStepVsSmootherStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothStepVsSmootherStepTest::RunTest(const FString& Parameters)
{
	// At T=0 and T=1 both functions return the same values (0 and 1).
	// At intermediate values they must differ.

	// T = 0
	UTEST_TRUE("SmoothStep(0) == 0", FMath::IsNearlyEqual(ComposableCameraSystem::SmoothStep(0.f), 0.f, 1e-6f));
	UTEST_TRUE("SmootherStep(0) == 0", FMath::IsNearlyEqual(ComposableCameraSystem::SmootherStep(0.f), 0.f, 1e-6f));

	// T = 1
	UTEST_TRUE("SmoothStep(1) == 1", FMath::IsNearlyEqual(ComposableCameraSystem::SmoothStep(1.f), 1.f, 1e-6f));
	UTEST_TRUE("SmootherStep(1) == 1", FMath::IsNearlyEqual(ComposableCameraSystem::SmootherStep(1.f), 1.f, 1e-6f));

	// T = 0.25 — they must produce different values.
	{
		float Smooth = ComposableCameraSystem::SmoothStep(0.25f);
		float Smoother = ComposableCameraSystem::SmootherStep(0.25f);
		UTEST_FALSE("SmoothStep(0.25) != SmootherStep(0.25)", FMath::IsNearlyEqual(Smooth, Smoother, 1e-6f));

		// SmoothStep(0.25) = 0.25^2 * (3 - 2*0.25) = 0.0625 * 2.5 = 0.15625
		UTEST_TRUE("SmoothStep(0.25) = 0.15625", FMath::IsNearlyEqual(Smooth, 0.15625f, 1e-6f));

		// SmootherStep(0.25) = 0.25^3 * (0.25 * (0.25 * 6 - 15) + 10)
		//                    = 0.015625 * (0.25 * (-13.5) + 10)
		//                    = 0.015625 * 6.625 = 0.103515625
		UTEST_TRUE("SmootherStep(0.25) = 0.103516", FMath::IsNearlyEqual(Smoother, 0.103515625f, 1e-5f));
	}

	// T = 0.5 — both are 0.5 at the midpoint (symmetric).
	{
		float Smooth = ComposableCameraSystem::SmoothStep(0.5f);
		float Smoother = ComposableCameraSystem::SmootherStep(0.5f);
		UTEST_TRUE("SmoothStep(0.5) = 0.5", FMath::IsNearlyEqual(Smooth, 0.5f, 1e-6f));
		UTEST_TRUE("SmootherStep(0.5) = 0.5", FMath::IsNearlyEqual(Smoother, 0.5f, 1e-6f));
	}

	// T = 0.75 — they must differ.
	{
		float Smooth = ComposableCameraSystem::SmoothStep(0.75f);
		float Smoother = ComposableCameraSystem::SmootherStep(0.75f);
		UTEST_FALSE("SmoothStep(0.75) != SmootherStep(0.75)", FMath::IsNearlyEqual(Smooth, Smoother, 1e-6f));
	}

	// Both functions should be monotonically increasing in [0, 1].
	{
		float PrevSmooth = 0.f;
		float PrevSmoother = 0.f;
		for (int i = 1; i <= 100; ++i)
		{
			float T = static_cast<float>(i) / 100.f;
			float S = ComposableCameraSystem::SmoothStep(T);
			float R = ComposableCameraSystem::SmootherStep(T);
			UTEST_TRUE("SmoothStep monotonic", S >= PrevSmooth - 1e-6f);
			UTEST_TRUE("SmootherStep monotonic", R >= PrevSmoother - 1e-6f);
			PrevSmooth = S;
			PrevSmoother = R;
		}
	}

	return true;
}

// ============================================================================
// Test: Director::ReactivateCurrentCamera with no running camera (BUG 4)
// Should return early without crashing.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDirectorReactivateNoRunningCameraTest,
	"System.Engine.ComposableCameraSystem.Director.ReactivateNoRunningCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDirectorReactivateNoRunningCameraTest::RunTest(const FString& Parameters)
{
	// Create a Director with no running camera.
	UComposableCameraDirector* Director = NewObject<UComposableCameraDirector>();

	// ReactivateCurrentCamera should return early (nullptr) without crashing.
	AComposableCameraCameraBase* Result = Director->ReactivateCurrentCamera(
		nullptr,
		AComposableCameraCameraBase::StaticClass(),
		nullptr,
		FOnCameraFinishConstructed{});

	UTEST_EQUAL("Returns nullptr when no running camera",
		Result, static_cast<AComposableCameraCameraBase*>(nullptr));

	return true;
}

#undef LOCTEXT_NAMESPACE
