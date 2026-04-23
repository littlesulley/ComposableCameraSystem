// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraCylindricalTransition.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Math/ComposableCameraMath.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowCylindricalTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Cylindrical"),
		0,
		TEXT("Show CylindricalTransition gizmo:\n")
		TEXT("  - Standard source/target/progress triplet in aqua accent.\n")
		TEXT("  - The actual curved cylindrical path sampled as a 32-segment polyline.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);

	// Spatial position along the cylindrical arc at parameter t ∈ [0, 1].
	//
	// Static file-local helper so the debug path can sample the whole curve
	// without re-implementing the math. OnEvaluate still inlines the same
	// formula because it also needs the intermediate StartPivot / TargetPivot
	// for its look-at rotation branch — splitting the pivot compute into a
	// separate helper would be pure ceremony for two call sites.
	//
	// ⚠ SYNC POINT: if you change the cylindrical formula here, mirror the
	// change in `UComposableCameraCylindricalTransition::OnEvaluate_Implementation`
	// (and vice versa). The debug polyline must trace the same curve the
	// camera actually follows.
	FVector SampleCylindricalPathPosition(
		float t,
		const FVector& SrcPos, const FRotator& SrcRot,
		const FVector& TgtPos, const FRotator& TgtRot,
		float MinimumDistanceFromOrigin)
	{
		FComposableCameraRayDefinition StartRay { SrcPos,
			SrcRot.RotateVector(FVector::ForwardVector), MinimumDistanceFromOrigin };
		FComposableCameraRayDefinition TargetRay { TgtPos,
			TgtRot.RotateVector(FVector::ForwardVector), MinimumDistanceFromOrigin };
		FComposableCameraNearestPointsOnRaysResult Result = StartRay.FindNearestPointsByOtherRay(TargetRay);

		const FVector StartPivot  = Result.FirstPoint;
		const FVector TargetPivot = Result.SecondPoint;

		const FVector StartDirection  = FVector::VectorPlaneProject(SrcPos - StartPivot,  FVector::UpVector);
		const FVector TargetDirection = FVector::VectorPlaneProject(TgtPos - TargetPivot, FVector::UpVector);
		const FVector ResultVector    = ComposableCameraSystem::Slerp(StartDirection, TargetDirection, t);

		const FVector StartResultPosition  = SrcPos + (ResultVector - StartDirection);
		const FVector TargetResultPosition = TgtPos + (ResultVector - TargetDirection);
		return FMath::Lerp(StartResultPosition, TargetResultPosition, t);
	}
}
#endif

void UComposableCameraCylindricalTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                        const FComposableCameraPose& CurrentSourcePose,
                                                                        const FComposableCameraPose& CurrentTargetPose)
{
}

FComposableCameraPose UComposableCameraCylindricalTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	// Position.
	FComposableCameraRayDefinition StartRay { CurrentSourcePose.Position, CurrentSourcePose.Rotation.RotateVector(FVector::ForwardVector), MinimumDistanceFromOrigin };
	FComposableCameraRayDefinition TargetRay { CurrentTargetPose.Position, CurrentTargetPose.Rotation.RotateVector(FVector::ForwardVector), MinimumDistanceFromOrigin };
	FComposableCameraNearestPointsOnRaysResult Result = StartRay.FindNearestPointsByOtherRay(TargetRay);

	float BlendPct = (TransitionTime - RemainingTime) / TransitionTime;
	BlendPct = ComposableCameraSystem::SmootherStep(BlendPct);
	Percentage = BlendPct;

	FVector StartPosition = CurrentSourcePose.Position;
	FVector StartPivot = Result.FirstPoint;
	FVector TargetPosition = CurrentTargetPose.Position;
	FVector TargetPivot = Result.SecondPoint;

	FVector StartDirection = FVector::VectorPlaneProject(StartPosition - StartPivot, FVector::UpVector);
	FVector TargetDirection = FVector::VectorPlaneProject(TargetPosition - TargetPivot, FVector::UpVector);
	FVector ResultVector = ComposableCameraSystem::Slerp(StartDirection, TargetDirection, BlendPct);

	FVector StartResultPosition = StartPosition + (ResultVector - StartDirection);
	FVector TargetResultPosition = TargetPosition + (ResultVector - TargetDirection);
	FVector ResultPosition = FMath::Lerp(StartResultPosition, TargetResultPosition, BlendPct);

	// Rotation, looking at the blended pivot or using vanilla rotation blend.
	FVector ResultPivot = FMath::Lerp(StartPivot, TargetPivot, BlendPct);
	FRotator ResultRotation =
		bLockToPivot ?
		UKismetMathLibrary::FindLookAtRotation(ResultPosition, ResultPivot) :
		(CurrentSourcePose.Rotation + BlendPct * (CurrentTargetPose.Rotation - CurrentSourcePose.Rotation).GetNormalized()).GetNormalized();

	// Blend ALL pose fields (FOV, physical camera, projection, etc.) using the shared BlendBy rule.
	// Position/Rotation are then overwritten below with the cylindrical path values — this transition's
	// whole point is that positional/rotational blending follow a curved trajectory, not a straight lerp.
	FComposableCameraPose ResultPose = CurrentSourcePose;
	ResultPose.BlendBy(CurrentTargetPose, BlendPct);
	ResultPose.Position = ResultPosition;
	ResultPose.Rotation = ResultRotation;

	// Debug draw lives on the `CCS.Debug.Viewport.Transitions.Cylindrical`
	// path now — see DrawTransitionDebug below.

	return ResultPose;
}

float UComposableCameraCylindricalTransition::GetBlendWeightAt(float NormalizedTime) const
{
	// Matches the SmootherStep OnEvaluate applies to BlendPct before the
	// spatial (Slerp + Lerp) evaluation.
	return ComposableCameraSystem::SmootherStep(FMath::Clamp(NormalizedTime, 0.f, 1.f));
}

#if !UE_BUILD_SHIPPING
void UComposableCameraCylindricalTransition::DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowCylindricalTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Aqua — vivid enough to stand out against the curved path, separate
	// from LookAt's pure cyan.
	static const FColor AccentColor { 100, 230, 200 };

	DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor);

	// Sample the actual cylindrical arc. 32 segments — cheap, and matches
	// the Spline / PathGuided resolution so overlapping transitions visually
	// look uniform. The polyline IS the transition's path, so the user sees
	// exactly where the camera will go, not a misleading straight line.
	constexpr int32 NumSamples = 32;
	const FVector SrcPos = LastDebugSource.Position;
	const FVector TgtPos = LastDebugTarget.Position;
	const FRotator SrcRot = LastDebugSource.Rotation;
	const FRotator TgtRot = LastDebugTarget.Rotation;

	FVector PrevPoint = SampleCylindricalPathPosition(0.f, SrcPos, SrcRot, TgtPos, TgtRot, MinimumDistanceFromOrigin);
	for (int32 i = 1; i <= NumSamples; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(NumSamples);
		const FVector NextPoint = SampleCylindricalPathPosition(t, SrcPos, SrcRot, TgtPos, TgtRot, MinimumDistanceFromOrigin);
		DrawDebugLine(World, PrevPoint, NextPoint, AccentColor,
			/*bPersistent=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/SDPG_Foreground, /*Thickness=*/1.f);
		PrevPoint = NextPoint;
	}
}
#endif
