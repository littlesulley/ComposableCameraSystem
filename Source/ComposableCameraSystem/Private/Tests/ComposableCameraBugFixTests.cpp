// Copyright Sulley. All rights reserved.

// Tests for bugs fixed during the codebase review pass.
// BUG 4: ReactivateCurrentCamera null dereference
// BUG 6: SplineTransition Smooth vs Smoother produce different results

#include "Math/ComposableCameraMath.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Interpolator/ComposableCameraIIRInterpolator.h"
#include "Nodes/ComposableCameraCollisionPushNode.h"
#include "Nodes/ComposableCameraLookAtNode.h"
#include "Nodes/ComposableCameraRotationConstraints.h"
#include "Nodes/ComposableCameraSetRotationNode.h"
#include "Nodes/ComposableCameraScreenSpacePivotNode.h"
#include "Nodes/ComposableCameraSpiralNode.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"

#define LOCTEXT_NAMESPACE "ComposableCameraBugFixTests"

namespace
{
	AActor* SpawnTestActorWithRoot(UWorld* World, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity) : nullptr;
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
}

// ============================================================================
// Test: IIR fixed-step mode progresses when callers reset every frame
// BUG: Nodes such as ScreenSpacePivot and Spline reset their IIR interpolators
//      every tick from the current value to the latest target. At frame rates
//      above 120fps, fixed-step IIR stored the substep remainder but Reset()
//      either cleared it before enough time accumulated or held the output
//      until a later frame, causing packaged high-FPS cameras to freeze or
//      visibly stutter.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIIRFixedStepPerFrameResetProgressesBelowMaxSubstepTest,
	"System.Engine.ComposableCameraSystem.Interpolator.IIR.FixedStepPerFrameResetProgressesBelowMaxSubstep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIIRFixedStepPerFrameResetProgressesBelowMaxSubstepTest::RunTest(const FString& Parameters)
{
	constexpr float SubframeDelta = 1.f / 240.f;

	TIIRInterpolator<double> Interpolator(10.f, true);
	Interpolator.Reset(0.0, 100.0);
	const double FirstValue = Interpolator.Run(SubframeDelta);

	Interpolator.Reset(FirstValue, 100.0);
	const double SecondValue = Interpolator.Run(SubframeDelta);

	UTEST_TRUE("Fixed-step IIR advances on the first sub-120Hz frame",
		FirstValue > 0.0);
	UTEST_TRUE("Repeated per-frame Reset below the fixed step advances every frame",
		SecondValue > FirstValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIIRFixedStepZeroDeltaKeepsCurrentValueTest,
	"System.Engine.ComposableCameraSystem.Interpolator.IIR.FixedStepZeroDeltaKeepsCurrentValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIIRFixedStepZeroDeltaKeepsCurrentValueTest::RunTest(const FString& Parameters)
{
	TIIRInterpolator<double> Interpolator(10.f, true);
	Interpolator.Reset(25.0, 100.0);
	const double Value = Interpolator.Run(0.f);

	UTEST_TRUE("Zero DeltaTime keeps the current value",
		FMath::IsNearlyEqual(Value, 25.0, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIIRFixedStepLargeDeltaUsesSubstepsTest,
	"System.Engine.ComposableCameraSystem.Interpolator.IIR.FixedStepLargeDeltaUsesSubsteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIIRFixedStepLargeDeltaUsesSubstepsTest::RunTest(const FString& Parameters)
{
	constexpr float StepDelta = 1.f / 120.f;
	constexpr float LargeDelta = 1.f / 30.f;
	constexpr double Target = 100.0;
	constexpr double Speed = 10.0;

	double Expected = 0.0;
	for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
	{
		Expected = FMath::FInterpTo(Expected, Target, StepDelta, Speed);
	}

	TIIRInterpolator<double> Interpolator(Speed, true);
	Interpolator.Reset(0.0, Target);
	const double Value = Interpolator.Run(LargeDelta);

	UTEST_TRUE("Large DeltaTime uses fixed-size substeps",
		FMath::IsNearlyEqual(Value, Expected, KINDA_SMALL_NUMBER));
	UTEST_TRUE("Large DeltaTime does not snap directly to target",
		Value < Target);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIIRNonFixedStepSubFrameDeltaProgressesTest,
	"System.Engine.ComposableCameraSystem.Interpolator.IIR.NonFixedStepSubFrameDeltaProgresses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIIRNonFixedStepSubFrameDeltaProgressesTest::RunTest(const FString& Parameters)
{
	TIIRInterpolator<double> Interpolator(10.f, false);
	Interpolator.Reset(0.0, 100.0);
	const double Value = Interpolator.Run(1.f / 240.f);

	UTEST_TRUE("Non-fixed IIR keeps original sub-frame behavior",
		Value > 0.0);

	return true;
}

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

	// T = 0.25. They must produce different values.
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

	// T = 0.5. Both are 0.5 at the midpoint (symmetric).
	{
		float Smooth = ComposableCameraSystem::SmoothStep(0.5f);
		float Smoother = ComposableCameraSystem::SmootherStep(0.5f);
		UTEST_TRUE("SmoothStep(0.5) = 0.5", FMath::IsNearlyEqual(Smooth, 0.5f, 1e-6f));
		UTEST_TRUE("SmootherStep(0.5) = 0.5", FMath::IsNearlyEqual(Smoother, 0.5f, 1e-6f));
	}

	// T = 0.75. They must differ.
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

// ============================================================================
// Test: LookAt preserves rotation when camera and target occupy the same point
// BUG: Spiral can place the camera exactly on its pivot (null/zero radius curve,
//      or a curve endpoint at radius 0). A downstream LookAt aimed at the same
//      pivot then asked FindLookAtRotation to solve a zero-length direction,
//      producing unstable/zero rotations instead of leaving the pose alone.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLookAtDegenerateTargetPreservesRotationTest,
	"System.Engine.ComposableCameraSystem.Nodes.LookAt.DegenerateTargetPreservesRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLookAtDegenerateTargetPreservesRotationTest::RunTest(const FString& Parameters)
{
	UComposableCameraLookAtNode* Node = NewObject<UComposableCameraLookAtNode>();
	Node->LookAtType = EComposableCameraLookAtType::ByPosition;
	Node->LookAtPosition = FVector::ZeroVector;
	Node->LookAtConstraintType = EComposableCameraLookAtConstraintType::Hard;
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Position = FVector::ZeroVector;
	InPose.Rotation = FRotator(12.f, 34.f, 5.f);

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	UTEST_TRUE("Degenerate look-at keeps upstream rotation",
		OutPose.Rotation.Equals(InPose.Rotation, KINDA_SMALL_NUMBER));

	return true;
}

// ============================================================================
// Test: Spiral PivotActorInitialForward captures the pivot actor forward once
// BUG: PivotActorForward is live every frame; when LookAt syncs camera rotation
//      into ControlRotation and the pawn follows it, Spiral's basis changes
//      every frame and feeds back into LookAt. PivotActorInitialForward should
//      keep the actor-authored starting direction while ignoring later yaw.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpiralPivotActorInitialForwardCapturesOnceTest,
	"System.Engine.ComposableCameraSystem.Nodes.Spiral.PivotActorInitialForwardCapturesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpiralPivotActorInitialForwardCapturesOnceTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* PivotActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator::ZeroRotator);

	UCurveFloat* RadiusCurve = NewObject<UCurveFloat>();
	RadiusCurve->FloatCurve.AddKey(0.f, 100.f);
	RadiusCurve->FloatCurve.AddKey(1.f, 100.f);

	UComposableCameraSpiralNode* Node = NewObject<UComposableCameraSpiralNode>();
	Node->PivotSourceType = EComposableCameraSpiralPivotSourceType::FromActor;
	Node->PivotActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->PivotActor = PivotActor;
	Node->RotationAxis = EComposableCameraSpiralRotationAxis::WorldUp;
	Node->ReferenceDirection = EComposableCameraSpiralReferenceDirection::PivotActorInitialForward;
	Node->RadiusCurve = RadiusCurve;
	Node->Duration = 10.f;
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Position = FVector::ZeroVector;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose FirstOutPose = InPose;
	Node->TickNode(0.016f, InPose, FirstOutPose);

	PivotActor->SetActorRotation(FRotator(0.f, 90.f, 0.f));

	FComposableCameraPose SecondOutPose = InPose;
	Node->TickNode(0.016f, InPose, SecondOutPose);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Initial pivot actor forward stays captured",
		FirstOutPose.Position.Equals(FVector(100.f, 0.f, 0.f), KINDA_SMALL_NUMBER));
	UTEST_TRUE("Later pivot actor yaw does not rotate the spiral basis",
		SecondOutPose.Position.Equals(FirstOutPose.Position, KINDA_SMALL_NUMBER));

	return true;
}

// ============================================================================
// Test: Lens-only physical settings do not write exposure settings
// BUG: Lens/DoF application should not secretly enable physical exposure or
// write ISO. UE owns the physical relationship between f-stop and exposure
// when AutoExposureApplyPhysicalCameraExposure is enabled.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLensOnlyDoesNotWriteExposureSettingsTest,
	"System.Engine.ComposableCameraSystem.Pose.LensOnlyDoesNotWriteExposureSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLensOnlyDoesNotWriteExposureSettingsTest::RunTest(const FString& Parameters)
{
	FComposableCameraPose Pose;
	Pose.PhysicalCameraBlendWeight = 1.f;
	Pose.ExposureBlendWeight = 0.f;
	Pose.ISO = 800.f;
	Pose.ShutterSpeed = 24.f;
	Pose.Aperture = 1.8f;
	Pose.FocusDistance = 500.f;

	FPostProcessSettings PostProcessSettings;
	PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = true;
	PostProcessSettings.DepthOfFieldFstop = 4.f;
	PostProcessSettings.CameraISO = 100.f;
	Pose.ApplyPhysicalCameraSettings(PostProcessSettings);

	UTEST_TRUE("DoF f-stop is written by PhysicalCameraBlendWeight",
		PostProcessSettings.bOverride_DepthOfFieldFstop);
	UTEST_TRUE("DoF focus is written by PhysicalCameraBlendWeight",
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance);
	UTEST_FALSE("ISO is not written by lens-only physical settings",
		PostProcessSettings.bOverride_CameraISO);
	UTEST_TRUE("ISO value is preserved",
		FMath::IsNearlyEqual(PostProcessSettings.CameraISO, 100.f, 1e-3f));
	UTEST_FALSE("Shutter is not written without ExposureBlendWeight",
		PostProcessSettings.bOverride_CameraShutterSpeed);
	UTEST_TRUE("Lens-only physical settings preserves existing physical exposure enabled state",
		PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure
		&& PostProcessSettings.AutoExposureApplyPhysicalCameraExposure);

	return true;
}

// ============================================================================
// Test: Lens-only physical settings do not enable physical exposure
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLensOnlyDoesNotEnablePhysicalExposureTest,
	"System.Engine.ComposableCameraSystem.Pose.LensOnlyDoesNotEnablePhysicalExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLensOnlyDoesNotEnablePhysicalExposureTest::RunTest(const FString& Parameters)
{
	FComposableCameraPose Pose;
	Pose.PhysicalCameraBlendWeight = 1.f;
	Pose.ExposureBlendWeight = 0.f;
	Pose.Aperture = 1.8f;
	Pose.FocusDistance = 500.f;

	FPostProcessSettings PostProcessSettings;
	PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Pose.ApplyPhysicalCameraSettings(PostProcessSettings);

	UTEST_TRUE("DoF f-stop is written by PhysicalCameraBlendWeight",
		PostProcessSettings.bOverride_DepthOfFieldFstop);
	UTEST_FALSE("Lens-only physical settings does not enable physical exposure",
		PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure);
	UTEST_FALSE("ISO is not written when physical exposure is disabled",
		PostProcessSettings.bOverride_CameraISO);

	return true;
}

// ============================================================================
// Test: DoF focus distance does not lerp out of Unreal's disabled sentinel
// BUG: DepthOfFieldFocalDistance default is 0, which UE treats as invalid /
// disabled. Lerp(0, Target, small weight) makes the focal plane only a few cm
// away during transitions, causing a full-screen blur spike.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPhysicalBlendWeightDoesNotScaleFocusFromDisabledSentinelTest,
	"System.Engine.ComposableCameraSystem.Pose.PhysicalBlendWeightDoesNotScaleFocusFromDisabledSentinel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPhysicalBlendWeightDoesNotScaleFocusFromDisabledSentinelTest::RunTest(const FString& Parameters)
{
	FComposableCameraPose Pose;
	Pose.PhysicalCameraBlendWeight = 0.1f;
	Pose.Aperture = 2.8f;
	Pose.FocusDistance = 500.f;

	FPostProcessSettings PostProcessSettings;
	Pose.ApplyPhysicalCameraSettings(PostProcessSettings);

	UTEST_TRUE("DoF focus is written by partial PhysicalCameraBlendWeight",
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance);
	UTEST_TRUE("DoF focus snaps from disabled sentinel to target focus",
		FMath::IsNearlyEqual(PostProcessSettings.DepthOfFieldFocalDistance, 500.f, 1e-3f));
	UTEST_TRUE("DoF scale fades in from zero",
		PostProcessSettings.bOverride_DepthOfFieldScale
		&& FMath::IsNearlyEqual(PostProcessSettings.DepthOfFieldScale, 0.1f, 1e-3f));

	return true;
}

// ============================================================================
// Test: ExposureBlendWeight applies only exposure
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExposureBlendWeightAppliesExposureOnlyTest,
	"System.Engine.ComposableCameraSystem.Pose.ExposureBlendWeightAppliesExposureOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExposureBlendWeightAppliesExposureOnlyTest::RunTest(const FString& Parameters)
{
	FComposableCameraPose Pose;
	Pose.PhysicalCameraBlendWeight = 0.f;
	Pose.ExposureBlendWeight = 1.f;
	Pose.ISO = 800.f;
	Pose.ShutterSpeed = 24.f;
	Pose.Aperture = 1.8f;
	Pose.FocusDistance = 500.f;

	FPostProcessSettings PostProcessSettings;
	Pose.ApplyPhysicalCameraSettings(PostProcessSettings);

	UTEST_TRUE("ISO is written by ExposureBlendWeight",
		PostProcessSettings.bOverride_CameraISO);
	UTEST_TRUE("Shutter is written by ExposureBlendWeight",
		PostProcessSettings.bOverride_CameraShutterSpeed);
	UTEST_FALSE("ExposureBlendWeight does not toggle physical camera exposure",
		PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure);
	UTEST_FALSE("DoF f-stop is not written without PhysicalCameraBlendWeight",
		PostProcessSettings.bOverride_DepthOfFieldFstop);
	UTEST_FALSE("DoF focus is not written without PhysicalCameraBlendWeight",
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance);

	return true;
}

// ============================================================================
// Test: ScreenSpacePivot ActorPosition uses the authored world-up offset
// BUG: GetCurrentPivot bypassed the already-resolved UPROPERTY values and read
//      the runtime data block directly, so Details-only values could resolve as
//      zero / null.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FScreenSpacePivotActorPositionUpOffsetTest,
	"System.Engine.ComposableCameraSystem.Nodes.ScreenSpacePivot.ActorPositionUsesUpOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FScreenSpacePivotActorPositionUpOffsetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FTransform CameraTransform;
	CameraTransform.SetLocation(FVector::ZeroVector);
	AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), CameraTransform);
	Camera->FinishSpawning(CameraTransform);

	AActor* PivotActor = SpawnTestActorWithRoot(
		World,
		FVector(1000.f, 0.f, 0.f),
		FRotator::ZeroRotator);

	UComposableCameraScreenSpacePivotNode* Node = NewObject<UComposableCameraScreenSpacePivotNode>();
	Node->PivotSource = EComposableCameraScreenSpacePivotSource::ActorPosition;
	Node->PivotActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->PivotActor = PivotActor;
	Node->PivotWorldUpOffset = 100.f;
	Node->PivotWorldPosition = FVector(1000.f, 0.f, 0.f);
	Node->Method = EComposableCameraScreenSpaceMethod::Rotate;
	Node->Initialize(Camera, nullptr);

	FComposableCameraPose InPose;
	InPose.Position = FVector::ZeroVector;
	InPose.Rotation = FRotator::ZeroRotator;
	InPose.SetFieldOfViewDegrees(90.f);

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const bool bPitchStayedZero = FMath::IsNearlyZero(OutPose.Rotation.Pitch, 0.01f);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_FALSE("ActorPosition up offset changes pitch", bPitchStayedZero);

	return true;
}

// ============================================================================
// Test: CollisionPush clears its previous position push before upstream nodes run
// BUG: OnPreTick wrote the unpushed position back to CameraPose, but TickCamera
//      had already copied CameraPose into the in-flight NewCameraPose, so upstream
//      nodes still saw the previous collision-pushed camera position.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCollisionPushPreTickRestoresInFlightPoseTest,
	"System.Engine.ComposableCameraSystem.Nodes.CollisionPush.PreTickRestoresInFlightPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCollisionPushPreTickRestoresInFlightPoseTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FTransform CameraTransform;
	CameraTransform.SetLocation(FVector::ZeroVector);
	AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), CameraTransform);
	Camera->FinishSpawning(CameraTransform);

	AActor* PivotActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator::ZeroRotator);

	UComposableCameraCollisionPushNode* Node = NewObject<UComposableCameraCollisionPushNode>();
	Node->PivotActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->PivotActor = PivotActor;
	Node->Initialize(Camera, nullptr);

	FComposableCameraPose PreCollisionPose;
	PreCollisionPose.Position = FVector(100.f, 0.f, 50.f);
	PreCollisionPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose TickOutPose = PreCollisionPose;
	Node->TickNode(0.016f, PreCollisionPose, TickOutPose);

	FComposableCameraPose NextFrameInFlightPose = PreCollisionPose;
	NextFrameInFlightPose.Position = FVector(25.f, 0.f, 50.f);
	Node->OnPreTick(0.016f, NextFrameInFlightPose, NextFrameInFlightPose);

	const bool bRestoredInFlightPose =
		NextFrameInFlightPose.Position.Equals(PreCollisionPose.Position, KINDA_SMALL_NUMBER);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("PreTick restores the in-flight pose, not only CameraPose", bRestoredInFlightPose);

	return true;
}

// ============================================================================
// Test: SetRotation writes the camera rotation from actor / vector / rotator
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationFromActorTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.FromActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationFromActorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* ReferenceActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator(20.f, 45.f, 35.f));

	FComposableCameraRuntimeDataBlock RuntimeData;
	RuntimeData.Storage.SetNumZeroed(static_cast<int32>(sizeof(AActor*)));
	RuntimeData.SlotShapes.Add(0, {
		EComposableCameraPinType::Actor,
		static_cast<int32>(sizeof(AActor*)),
		nullptr
	});
	FMemory::Memcpy(RuntimeData.Storage.GetData(), &ReferenceActor, sizeof(AActor*));
	RuntimeData.DefaultValueOffsets.Add(FComposableCameraPinKey{0, TEXT("RotationActor")}, 0);

	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromActor;
	Node->RotationActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const FRotator Expected = UKismetMathLibrary::MakeRotFromX(ReferenceActor->GetActorForwardVector());

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("SetRotation FromActor uses actor forward",
		OutPose.Rotation.Equals(Expected, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationFromVectorTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.FromVector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationFromVectorTest::RunTest(const FString& Parameters)
{
	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromVector;
	Node->RotationVector = FVector(1.f, 1.f, 1.f);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const FRotator Expected = UKismetMathLibrary::MakeRotFromX(Node->RotationVector);
	UTEST_TRUE("SetRotation FromVector uses MakeRotFromX",
		OutPose.Rotation.Equals(Expected, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationFromRotatorTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.FromRotator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationFromRotatorTest::RunTest(const FString& Parameters)
{
	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromRotator;
	Node->Rotation = FRotator(12.f, 34.f, 5.f);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	UTEST_TRUE("SetRotation FromRotator writes literal rotation",
		OutPose.Rotation.Equals(Node->Rotation, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBeginPlaySetRotationFromRotatorTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.BeginPlayFromRotator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBeginPlaySetRotationFromRotatorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FTransform CameraTransform;
	CameraTransform.SetLocation(FVector::ZeroVector);
	AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), CameraTransform);
	Camera->FinishSpawning(CameraTransform);
	Camera->CameraPose.Rotation = FRotator::ZeroRotator;

	UComposableCameraBeginPlaySetRotationNode* Node = NewObject<UComposableCameraBeginPlaySetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromRotator;
	Node->Rotation = FRotator(11.f, 22.f, 3.f);
	Node->Initialize(Camera, nullptr);
	Node->ExecuteBeginPlay();

	const FRotator ResultRotation = Camera->CameraPose.Rotation;
	const FRotator ResultLastFrameRotation = Camera->LastFrameCameraPose.Rotation;

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("BeginPlay SetRotation writes initial camera rotation",
		ResultRotation.Equals(Node->Rotation, 0.01f));
	UTEST_TRUE("BeginPlay SetRotation seeds last-frame camera rotation",
		ResultLastFrameRotation.Equals(Node->Rotation, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBeginPlaySetRotationSeedsRotationConstraintsTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.BeginPlaySeedsRotationConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBeginPlaySetRotationSeedsRotationConstraintsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FTransform CameraTransform;
	CameraTransform.SetLocation(FVector::ZeroVector);
	AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), CameraTransform);
	Camera->FinishSpawning(CameraTransform);
	Camera->CameraPose.Rotation = FRotator(0.f, 180.f, 0.f);
	Camera->LastFrameCameraPose.Rotation = Camera->CameraPose.Rotation;

	AActor* PivotActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator(0.f, 90.f, 0.f));

	UComposableCameraBeginPlaySetRotationNode* InitialRotationNode = NewObject<UComposableCameraBeginPlaySetRotationNode>();
	InitialRotationNode->RotationSource = EComposableCameraSetRotationSource::FromActor;
	InitialRotationNode->RotationActorSource = EComposableCameraActorInputSource::ExplicitActor;
	InitialRotationNode->RotationActor = PivotActor;
	InitialRotationNode->Initialize(Camera, nullptr);
	InitialRotationNode->ExecuteBeginPlay();

	UComposableCameraRotationConstraints* ConstraintNode = NewObject<UComposableCameraRotationConstraints>();
	ConstraintNode->bConstrainYaw = true;
	ConstraintNode->ConstrainYawType = EComposableCameraRotationConstrainType::ActorSpace;
	ConstraintNode->ActorForYawConstrainSource = EComposableCameraActorInputSource::ExplicitActor;
	ConstraintNode->ActorForYawConstrain = PivotActor;
	ConstraintNode->YawRange = FVector2D(-10.f, 10.f);
	ConstraintNode->bConstrainPitch = false;
	ConstraintNode->Initialize(Camera, nullptr);

	FComposableCameraPose OutPose = Camera->CameraPose;
	ConstraintNode->TickNode(0.016f, Camera->CameraPose, OutPose);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("RotationConstraints starts from pivot forward instead of clamping old camera yaw",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(OutPose.Rotation.Yaw, 90.f), 0.f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBeginPlaySetRotationFeedsFirstRuntimeTickTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.BeginPlayFeedsFirstRuntimeTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBeginPlaySetRotationFeedsFirstRuntimeTickTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FTransform CameraTransform;
	CameraTransform.SetLocation(FVector::ZeroVector);
	AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), CameraTransform);
	Camera->FinishSpawning(CameraTransform);
	Camera->CameraPose.Rotation = FRotator(0.f, 180.f, 0.f);
	Camera->LastFrameCameraPose.Rotation = Camera->CameraPose.Rotation;

	AActor* PivotActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator(0.f, 90.f, 0.f));

	UComposableCameraBeginPlaySetRotationNode* InitialRotationNode = NewObject<UComposableCameraBeginPlaySetRotationNode>();
	InitialRotationNode->RotationSource = EComposableCameraSetRotationSource::FromActor;
	InitialRotationNode->RotationActorSource = EComposableCameraActorInputSource::ExplicitActor;
	InitialRotationNode->RotationActor = PivotActor;

	UComposableCameraRotationConstraints* ConstraintNode = NewObject<UComposableCameraRotationConstraints>();
	ConstraintNode->bConstrainYaw = true;
	ConstraintNode->ConstrainYawType = EComposableCameraRotationConstrainType::ActorSpace;
	ConstraintNode->ActorForYawConstrainSource = EComposableCameraActorInputSource::ExplicitActor;
	ConstraintNode->ActorForYawConstrain = PivotActor;
	ConstraintNode->YawRange = FVector2D(-10.f, 10.f);
	ConstraintNode->bConstrainPitch = false;

	Camera->ComputeNodes.Add(InitialRotationNode);
	Camera->CameraNodes.Add(ConstraintNode);
	Camera->OwnedRuntimeDataBlock = MakeUnique<FComposableCameraRuntimeDataBlock>();
	Camera->OwnedRuntimeDataBlock->Storage.SetNumZeroed(static_cast<int32>(sizeof(AActor*)));
	Camera->OwnedRuntimeDataBlock->TotalSize = static_cast<int32>(sizeof(AActor*));
	Camera->TypeAssetNodeTemplateCount = Camera->CameraNodes.Num();

	const int32 ComputeRotationActorOffset = 0;
	Camera->OwnedRuntimeDataBlock->SlotShapes.Add(ComputeRotationActorOffset, {
		EComposableCameraPinType::Actor,
		static_cast<int32>(sizeof(AActor*)),
		nullptr
	});
	FMemory::Memcpy(
		Camera->OwnedRuntimeDataBlock->Storage.GetData() + ComputeRotationActorOffset,
		&PivotActor,
		sizeof(AActor*));
	Camera->OwnedRuntimeDataBlock->DefaultValueOffsets.Add(
		FComposableCameraPinKey{Camera->TypeAssetNodeTemplateCount, TEXT("RotationActor")},
		ComputeRotationActorOffset);

	FComposableCameraExecEntry ComputeEntry;
	ComputeEntry.EntryType = EComposableCameraExecEntryType::CameraNode;
	ComputeEntry.CameraNodeIndex = 0;
	Camera->ComputeFullExecChain.Add(ComputeEntry);

	FComposableCameraExecEntry TickEntry;
	TickEntry.EntryType = EComposableCameraExecEntryType::CameraNode;
	TickEntry.CameraNodeIndex = 0;
	Camera->FullExecChain.Add(TickEntry);

	InitialRotationNode->SetRuntimeDataBlock(Camera->OwnedRuntimeDataBlock.Get(), Camera->TypeAssetNodeTemplateCount);
	ConstraintNode->SetRuntimeDataBlock(Camera->OwnedRuntimeDataBlock.Get(), 0);
	Camera->InitializeNodes();
	Camera->BeginPlayCamera();

	const FRotator BeginPlayRotation = Camera->CameraPose.Rotation;

	Camera->LastTickedFrameCounter = TNumericLimits<uint64>::Max();
	const FComposableCameraPose FirstTickPose = Camera->TickCamera(0.016f);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("BeginPlay chain sets camera to pivot actor forward before the first tick",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(BeginPlayRotation.Yaw, 90.f), 0.f, 0.01f));
	UTEST_TRUE("First TickCamera keeps the pivot-forward yaw inside RotationConstraints",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(FirstTickPose.Rotation.Yaw, 90.f), 0.f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FActorPinDefaultOverrideDoesNotCreateNullRuntimeDefaultTest,
	"System.Engine.ComposableCameraSystem.TypeAsset.ActorPinDefaultOverrideDoesNotCreateNullRuntimeDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorPinDefaultOverrideDoesNotCreateNullRuntimeDefaultTest::RunTest(const FString& Parameters)
{
	UComposableCameraTypeAsset* TypeAsset = NewObject<UComposableCameraTypeAsset>();

	UComposableCameraBeginPlaySetRotationNode* ComputeNode =
		NewObject<UComposableCameraBeginPlaySetRotationNode>(TypeAsset);
	ComputeNode->RotationSource = EComposableCameraSetRotationSource::FromActor;
	TypeAsset->ComputeNodeTemplates.Add(ComputeNode);

	FComposableCameraNodeTemplatePinOverrides PinOverrides;
	FComposableCameraPinOverride ActorOverride;
	ActorOverride.PinName = TEXT("RotationActor");
	ActorOverride.bHasDefaultOverride = true;
	ActorOverride.DefaultValueOverride = TEXT("/Game/Maps/TestMap.TestMap:PersistentLevel.PivotActor");
	PinOverrides.Overrides.Add(ActorOverride);
	TypeAsset->ComputeNodePinOverrides.Add(PinOverrides);

	const FComposableCameraRuntimeDataBlock RuntimeData = TypeAsset->BuildRuntimeDataLayout();

	UTEST_FALSE("Actor per-instance defaults are not represented as null runtime defaults",
		RuntimeData.DefaultValueOffsets.Contains(FComposableCameraPinKey{0, TEXT("RotationActor")}));

	return true;
}

// ============================================================================
// Test: RotationConstraints ActorSpace reads explicit actors through the
// standard pin-to-UPROPERTY auto-resolution path.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRotationConstraintsActorSpaceUsesExplicitActorPinDefaultsTest,
	"System.Engine.ComposableCameraSystem.Nodes.RotationConstraints.ActorSpaceUsesExplicitActorPinDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRotationConstraintsActorSpaceUsesExplicitActorPinDefaultsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* ReferenceActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator(30.f, 90.f, 0.f));

	FComposableCameraRuntimeDataBlock RuntimeData;
	RuntimeData.Storage.SetNumZeroed(static_cast<int32>(sizeof(AActor*)) * 2);

	const int32 YawActorOffset = 0;
	const int32 PitchActorOffset = static_cast<int32>(sizeof(AActor*));
	RuntimeData.SlotShapes.Add(YawActorOffset, {
		EComposableCameraPinType::Actor,
		static_cast<int32>(sizeof(AActor*)),
		nullptr
	});
	RuntimeData.SlotShapes.Add(PitchActorOffset, {
		EComposableCameraPinType::Actor,
		static_cast<int32>(sizeof(AActor*)),
		nullptr
	});

	FMemory::Memcpy(RuntimeData.Storage.GetData() + YawActorOffset, &ReferenceActor, sizeof(AActor*));
	FMemory::Memcpy(RuntimeData.Storage.GetData() + PitchActorOffset, &ReferenceActor, sizeof(AActor*));
	RuntimeData.DefaultValueOffsets.Add(FComposableCameraPinKey{0, TEXT("ActorForYawConstrain")}, YawActorOffset);
	RuntimeData.DefaultValueOffsets.Add(FComposableCameraPinKey{0, TEXT("ActorForPitchConstrain")}, PitchActorOffset);

	UComposableCameraRotationConstraints* Node = NewObject<UComposableCameraRotationConstraints>();
	Node->bConstrainYaw = true;
	Node->ConstrainYawType = EComposableCameraRotationConstrainType::ActorSpace;
	Node->ActorForYawConstrainSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->YawRange = FVector2D(-10.f, 10.f);

	Node->bConstrainPitch = true;
	Node->ConstrainPitchType = EComposableCameraRotationConstrainType::ActorSpace;
	Node->ActorForPitchConstrainSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->PitchRange = FVector2D(-5.f, 5.f);

	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);

	UTEST_TRUE("ResolveAllInputPins writes yaw actor into UPROPERTY",
		Node->ActorForYawConstrain.Get() == ReferenceActor);
	UTEST_TRUE("ResolveAllInputPins writes pitch actor into UPROPERTY",
		Node->ActorForPitchConstrain.Get() == ReferenceActor);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator(50.f, 120.f, 0.f);

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	AddInfo(FString::Printf(
		TEXT("RotationConstraints ActorSpace result: Yaw=%.6f Pitch=%.6f, ExpectedYaw=100.000000 ExpectedPitch=35.000000"),
		OutPose.Rotation.Yaw,
		OutPose.Rotation.Pitch));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Yaw range is centered on explicit actor forward",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(OutPose.Rotation.Yaw, 100.f), 0.f, 0.01f));
	UTEST_TRUE("Pitch range is centered on explicit actor forward",
		FMath::IsNearlyEqual(OutPose.Rotation.Pitch, 35.f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRotationConstraintsActorSpaceYawRangeWrapsAroundTest,
	"System.Engine.ComposableCameraSystem.Nodes.RotationConstraints.ActorSpaceYawRangeWrapsAround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRotationConstraintsActorSpaceYawRangeWrapsAroundTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* ReferenceActor = SpawnTestActorWithRoot(
		World,
		FVector::ZeroVector,
		FRotator(0.f, 170.f, 0.f));

	UComposableCameraRotationConstraints* Node = NewObject<UComposableCameraRotationConstraints>();
	Node->bConstrainYaw = true;
	Node->ConstrainYawType = EComposableCameraRotationConstrainType::ActorSpace;
	Node->ActorForYawConstrainSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->ActorForYawConstrain = ReferenceActor;
	Node->YawRange = FVector2D(-30.f, 30.f);
	Node->bConstrainPitch = false;
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator(0.f, -175.f, 0.f);

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Yaw inside actor-relative range across +/-180 is preserved",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(OutPose.Rotation.Yaw, -175.f), 0.f, 0.01f));

	return true;
}

#undef LOCTEXT_NAMESPACE
