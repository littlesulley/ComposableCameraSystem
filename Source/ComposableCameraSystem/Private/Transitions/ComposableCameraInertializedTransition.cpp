// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraInertializedTransition.h"

#include "ComposableCameraSystemModule.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SceneManagement.h"   // SDPG_Foreground

namespace
{
	static TAutoConsoleVariable<int32> CVarShowInertializedTransitionGizmo(
		TEXT("CCS.Debug.Viewport.Transitions.Inertialized"),
		0,
		TEXT("Show InertializedTransition gizmo:\n")
		TEXT("  - Standard source/target/progress triplet in hot-pink accent.\n")
		TEXT("  - The polynomial path sampled as a 32-segment polyline. Usually\n")
		TEXT("    overshoots the straight source-to-target line. That overshoot\n")
		TEXT("    IS the signature of inertialization.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Gizmo disappears when the transition finishes."),
		ECVF_Default);

	constexpr int32 InertializedDebugSampleCount = 32;
}
#endif

template struct ComposableCameraInitializer<FVector, ComposableCameraPositionalInertializer>;
template struct ComposableCameraInitializer<FVector, ComposableCameraIndependentPositionalInertializer>;
template struct ComposableCameraInitializer<FRotator, ComposableCameraRotationalInertializer>;

void UComposableCameraInertializedTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                         const FComposableCameraPose& CurrentSourcePose,
                                                                         const FComposableCameraPose& CurrentTargetPose)
{
	// Use the previous and current source poses from InitParams for velocity estimation.
	// These are the Director's blended output poses from the previous and current frames.
	const FComposableCameraPose& LastSourceCameraPose = InitParams.PreviousSourcePose;
	const FComposableCameraPose& ThisSourceCameraPose = InitParams.CurrentSourcePose;
	const float InitDeltaTime = InitParams.DeltaTime;

	TransitionTime = GetActualBlendTime(InitDeltaTime, LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose);
	RemainingTime = TransitionTime;

	PositionalInertializer = ComposableCameraInitializer<FVector, ComposableCameraIndependentPositionalInertializer>{ LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose, TransitionTime, InitDeltaTime };
	RotationalInertializer = ComposableCameraInitializer<FRotator, ComposableCameraRotationalInertializer>{ LastSourceCameraPose, ThisSourceCameraPose, CurrentTargetPose, TransitionTime, InitDeltaTime };

#if !UE_BUILD_SHIPPING
	// Pre-sample the positional polynomial into target-independent offsets.
	// The runtime formula `Evaluate(blendDuration, Target) = Poly(blendDuration) + Target`
	// means passing a zero Target recovers the pure polynomial offset; adding
	// the live target position at draw time reproduces the real camera path.
	// Sampling now (once) keeps the draw path O(NumSamples) line writes per
	// frame with zero math. Critical because the hot-path gizmo walker
	// visits every active transition every tick.
	DebugPathOffsets.Reset();
	DebugPathOffsets.Reserve(InertializedDebugSampleCount + 1);
	for (int32 i = 0; i <= InertializedDebugSampleCount; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(InertializedDebugSampleCount);
		const float BlendDuration = t * TransitionTime;
		// Direct copy of OnEvaluate's `AdditiveCurve ? ... : PositionalInertializer.Evaluate(bd, target)`
		// path for the no-curve branch. For the curve-driven branch the
		// exact path is only defined at evaluation time (depends on target
		// at that tick), so we deliberately show the non-additive baseline
		// curve. Users who care about the additive shape read the debug
		// panel instead.
		DebugPathOffsets.Add(PositionalInertializer.Evaluate(BlendDuration, FVector::ZeroVector));
	}
#endif
}

FComposableCameraPose UComposableCameraInertializedTransition::OnEvaluate_Implementation(float DeltaTime,
                                                                                         const FComposableCameraPose& CurrentSourcePose,
                                                                                         const FComposableCameraPose& CurrentTargetPose)
{
	const float BlendDuration = TransitionTime - RemainingTime;
	const float BlendPct = BlendDuration / TransitionTime;
	Percentage = BlendPct;

	// Blend ALL pose fields (FOV, physical camera, projection, etc.) using the shared rule.
	// Position/Rotation are then overwritten below with inertialized values; rotation still
	// receives the base transition's live endpoint offsets so control input cannot re-path it.
	FComposableCameraPose OutPose = BlendPosesByLockedRotationPath(
		CurrentSourcePose,
		CurrentTargetPose,
		BlendPct);

	if (AdditiveCurve)
	{
		OutPose.Position = PositionalInertializer.Evaluate(BlendDuration, CurrentTargetPose.Position, BlendPct, AdditiveCurve, AdditiveCurveWeight, AdditiveCurveShape);
		const FRotator BaseRotation = RotationalInertializer.Evaluate(
			BlendDuration,
			GetInitialTargetRotation(),
			BlendPct,
			AdditiveCurve,
			AdditiveCurveWeight,
			AdditiveCurveShape);
		OutPose.Rotation = ApplyLiveRotationOffsetsToBaseRotation(BaseRotation, BlendPct);
	}
	else
	{
		OutPose.Position = PositionalInertializer.Evaluate(BlendDuration, CurrentTargetPose.Position);
		const FRotator BaseRotation = RotationalInertializer.Evaluate(
			BlendDuration,
			GetInitialTargetRotation());
		OutPose.Rotation = ApplyLiveRotationOffsetsToBaseRotation(BaseRotation, BlendPct);
	}

	return OutPose;
}

float UComposableCameraInertializedTransition::GetActualBlendTime(float InDeltaTime,
                                                                  const FComposableCameraPose& LastSourceCameraPose,
                                                                  const FComposableCameraPose& ThisSourceCameraPose,
                                                                  const FComposableCameraPose& CurrentTargetPose)
{
	if (bAutoTransitionTime)
	{
		auto [Velocity, Length] = [&]() -> auto
		{
			FVector LastCameraLocation = LastSourceCameraPose.Position;
			FVector ThisCameraLocation = ThisSourceCameraPose.Position;
			FVector InitialDirection = ThisCameraLocation - CurrentTargetPose.Position;
			FVector PreviousDirection = LastCameraLocation - CurrentTargetPose.Position;
			float InitialMagnitude = InitialDirection.Length();
			InitialDirection.Normalize();
			float PreviousMagnitude = PreviousDirection.Dot(InitialDirection);
			float InitialVelocity = (InitialMagnitude - PreviousMagnitude) / InDeltaTime;
			return std::pair{ InitialVelocity, InitialMagnitude };
		}();

		float AutoBlendTime = 1.f;
		float Sqrt = 64 * Velocity + 80 * MaxAcceleration * Length;
		if (Sqrt >= 0)
		{
			AutoBlendTime = FMath::Max(AutoBlendTime, (8 * Velocity + FMath::Sqrt(Sqrt)) / (2 * MaxAcceleration));
		}
		Sqrt = 64 * Velocity - 80 * MaxAcceleration * Length;
		if (Sqrt >= 0)
		{
			AutoBlendTime = FMath::Max(AutoBlendTime, (-8 * Velocity + FMath::Sqrt(Sqrt)) / (2 * MaxAcceleration));
		}

		return AutoBlendTime;
	}
	else
	{
		return TransitionTime;
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraInertializedTransition::DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	if (!World) { return; }
	if (CVarShowInertializedTransitionGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()) { return; }

	// Hot pink. Strongly distinct accent because inertialization's overshoot
	// is the most visually interesting thing the debug draw reveals.
	static const FColor AccentColor { 255, 100, 200 };

	DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor);

	// DebugPathOffsets is populated by OnBeginPlay; if for some reason it
	// is empty (pre-OnBeginPlay frame, or a sub-transition hasn't been
	// initialized yet), skip the polyline rather than drawing garbage.
	if (DebugPathOffsets.Num() < 2)
	{
		return;
	}

	// Reconstitute the world-space path by adding this frame's target
	// position to every cached offset. A moving target during the blend
	// (live camera actor) still produces a visually consistent path -	// offsets are target-relative by construction.
	const FVector TgtPos = LastDebugTarget.Position;
	FVector PrevPoint = DebugPathOffsets[0] + TgtPos;
	for (int32 i = 1; i < DebugPathOffsets.Num(); ++i)
	{
		const FVector NextPoint = DebugPathOffsets[i] + TgtPos;
		DrawDebugLine(World, PrevPoint, NextPoint, AccentColor,
			/*bPersistent=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/SDPG_Foreground, /*Thickness=*/1.f);
		PrevPoint = NextPoint;
	}
}
#endif
