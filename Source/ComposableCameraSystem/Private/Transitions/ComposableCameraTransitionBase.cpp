// Copyright 2026 Sulley. All Rights Reserved.

#include "Transitions/ComposableCameraTransitionBase.h"

#include "ComposableCameraSystemModule.h"   // STATGROUP_CCS
#include "Core/ComposableCameraPlayerCameraManager.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "SceneManagement.h"   // SDPG_Foreground
#endif

DECLARE_CYCLE_STAT(TEXT("Transition Evaluate"), STAT_CCS_Transition_Evaluate, STATGROUP_CCS);

FComposableCameraPose UComposableCameraTransitionBase::Evaluate(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_Transition_Evaluate);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Transition_Evaluate);
	// Lazy-cache the class name once per transition instance so the dynamic
	// Insights label stops allocating an FString per evaluate. Class is
	// immutable after construction; one cheap IsEmpty check per call.
	if (TransitionClassTraceName.IsEmpty())
	{
		TransitionClassTraceName = GetClass()->GetName();
	}
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(*TransitionClassTraceName);

	if (bFirstFrame)
	{
		InitializeLockedRotationPathState(CurrentSourcePose, CurrentTargetPose);
		OnBeginPlay(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	}
	else
	{
		UpdateLockedRotationPathState(CurrentSourcePose, CurrentTargetPose);
	}

	// If no time remains, directly return the target pose.
	RemainingTime -= DeltaTime;
	if (RemainingTime <= 0.0f)
	{
		TransitionFinished();
		return CurrentTargetPose;
	}

	// Else, do evaluation.
	FComposableCameraPose OutResult = OnEvaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	bFirstFrame = false;

	return OutResult;
}

void UComposableCameraTransitionBase::TransitionEnabled(const FComposableCameraTransitionInitParams& InInitParams)
{
	InitParams = InInitParams;
	bFinished = false;
	CachedPlayerCameraManager = GetTypedOuter<AComposableCameraPlayerCameraManager>();
}

void UComposableCameraTransitionBase::TransitionFinished()
{
	bFinished = true;

	if (OnTransitionFinishesDelegate.IsBound())
	{
		OnTransitionFinishesDelegate.Broadcast();
		OnTransitionFinishesDelegate.Clear();
	}

	OnFinished();
}

void UComposableCameraTransitionBase::SetTransitionTime(float NewTransitionTime)
{
	TransitionTime = NewTransitionTime;
}

void UComposableCameraTransitionBase::ResetTransitionState()
{
	RemainingTime = TransitionTime;
	bFinished = false;
	bFirstFrame = true;
	bHasLockedRotationPathState = false;
	InitialSourceRotation = FRotator::ZeroRotator;
	InitialTargetRotation = FRotator::ZeroRotator;
	InitialRotationDelta = FRotator::ZeroRotator;
	PreviousSourceRotation = FRotator::ZeroRotator;
	PreviousTargetRotation = FRotator::ZeroRotator;
	AccumulatedSourceRotationOffset = FRotator::ZeroRotator;
	AccumulatedTargetRotationOffset = FRotator::ZeroRotator;
}

FComposableCameraPose UComposableCameraTransitionBase::BlendPosesByLockedRotationPath(
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose,
	float TargetWeight) const
{
	FComposableCameraPose ResultPose = CurrentSourcePose;
	ResultPose.BlendBy(CurrentTargetPose, TargetWeight);
	ResultPose.Rotation = BlendRotationByLockedPath(TargetWeight);
	return ResultPose;
}

FRotator UComposableCameraTransitionBase::BlendRotationByLockedPath(float TargetWeight) const
{
	const float ClampedWeight = FMath::Clamp(TargetWeight, 0.f, 1.f);
	const FRotator BaseRotation =
		(InitialSourceRotation + InitialRotationDelta * ClampedWeight).GetNormalized();
	return ApplyLiveRotationOffsetsToBaseRotation(BaseRotation, ClampedWeight);
}

FRotator UComposableCameraTransitionBase::ApplyLiveRotationOffsetsToBaseRotation(
	const FRotator& BaseRotation,
	float TargetWeight) const
{
	const float ClampedWeight = FMath::Clamp(TargetWeight, 0.f, 1.f);
	const FRotator SourceOffset = AccumulatedSourceRotationOffset * (1.f - ClampedWeight);
	const FRotator TargetOffset = AccumulatedTargetRotationOffset * ClampedWeight;
	return (BaseRotation + SourceOffset + TargetOffset).GetNormalized();
}

void UComposableCameraTransitionBase::InitializeLockedRotationPathState(
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose)
{
	InitialSourceRotation = CurrentSourcePose.Rotation;
	InitialTargetRotation = CurrentTargetPose.Rotation;
	InitialRotationDelta = (InitialTargetRotation - InitialSourceRotation).GetNormalized();
	PreviousSourceRotation = InitialSourceRotation;
	PreviousTargetRotation = InitialTargetRotation;
	AccumulatedSourceRotationOffset = FRotator::ZeroRotator;
	AccumulatedTargetRotationOffset = FRotator::ZeroRotator;
	bHasLockedRotationPathState = true;
}

void UComposableCameraTransitionBase::UpdateLockedRotationPathState(
	const FComposableCameraPose& CurrentSourcePose,
	const FComposableCameraPose& CurrentTargetPose)
{
	if (!bHasLockedRotationPathState)
	{
		InitializeLockedRotationPathState(CurrentSourcePose, CurrentTargetPose);
		return;
	}

	AccumulatedSourceRotationOffset += (CurrentSourcePose.Rotation - PreviousSourceRotation).GetNormalized();
	AccumulatedTargetRotationOffset += (CurrentTargetPose.Rotation - PreviousTargetRotation).GetNormalized();
	PreviousSourceRotation = CurrentSourcePose.Rotation;
	PreviousTargetRotation = CurrentTargetPose.Rotation;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraTransitionBase::DrawStandardTransitionDebug(
	UWorld* World, bool bViewerIsOutsideCamera, const FColor& AccentColor) const
{
	if (!World) { return; }

	// Color legend (fixed across every transition type):
	//   Source->green . Where the blend is coming FROM this frame
	//   Target->blue  . Where the blend is going TO this frame
	//   Progress->accent. The actual blended camera position this frame
	//                      (the per-transition AccentColor also identifies
	//                       which transition type is contributing when
	//                       multiple are active simultaneously)
	//
	// This helper is deliberately silent about the PATH between source
	// and target. Path shape depends on the transition type (straight for
	// Linear/Smooth/Ease/Cubic, arc for Cylindrical, polynomial for
	// Inertialized, authored curve for Spline, rail for PathGuided). Each
	// concrete override draws its own path polyline in the same AccentColor
	// on top of this helper's markers. See Section 3.20.4 in TechDoc.
	static const FColor SourceColor { 80, 220, 120 };
	static const FColor TargetColor { 80, 170, 255 };

	const FVector SrcPos   = LastDebugSource.Position;
	const FVector TgtPos   = LastDebugTarget.Position;
	const FVector BlendPos = LastDebugBlended.Position;

	// Sphere markers. Solid translucent via DrawSolidDebugSphere so the
	// gizmo reads as a filled VOLUME rather than a busy wireframe. The
	// source/target endpoint spheres stay subtle (alpha 100 = ~39 %)
	// while the accent progress sphere is bumped up a touch (alpha 130)
	// so the camera's live position POPs relative to the fixed endpoints.
	// DepthPriority=SDPG_Foreground so the ball draws above scene
	// geometry even when sitting inside a mesh (e.g. camera embedded
	// in a character), same rule as the retired wireframe path.
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, SrcPos, /*Radius=*/7.5f, SourceColor,
		/*Alpha=*/100, /*Segments=*/12, /*DepthPriority=*/SDPG_Foreground);
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, TgtPos, 7.5f, TargetColor,
		100, 12, SDPG_Foreground);
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, BlendPos, 10.f, AccentColor,
		/*Alpha=*/130, 12, SDPG_Foreground);

	// Frustums only outside possess. The blended frustum is already painted
	// by the camera-level pass (AComposableCameraCameraBase::DrawCameraDebug
	// with bDrawFrustum=true) so we skip it here to avoid stacking on the
	// same pose. In possessed play the frustums would occlude the near
	// plane even at half scale, so they're gated out.
	if (bViewerIsOutsideCamera)
	{
		DrawDebugCamera(World, SrcPos, LastDebugSource.Rotation, LastDebugSource.FOVDegrees,
			/*Scale=*/0.5f, SourceColor, /*bPersistent=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/0);
		DrawDebugCamera(World, TgtPos, LastDebugTarget.Rotation, LastDebugTarget.FOVDegrees,
			0.5f, TargetColor, false, -1.f, 0);
	}
}
#endif // !UE_BUILD_SHIPPING
