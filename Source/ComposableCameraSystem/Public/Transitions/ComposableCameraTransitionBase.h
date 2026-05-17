// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "ComposableCameraTransitionBase.generated.h"

class UComposableCameraTransitionBase;

DECLARE_MULTICAST_DELEGATE(FOnTransitionFinishes);

/**
 * Parameters passed when a transition is initialized.
 * Contains the source pose data needed for transitions to set up their internal state.
 */
USTRUCT(BlueprintType)
struct FComposableCameraTransitionInitParams
{
	GENERATED_BODY()

	/** Source pose at the moment the transition starts (the blended output the player was seeing). */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	FComposableCameraPose CurrentSourcePose;

	/** Previous frame's source pose (for velocity-based transitions like inertialization). */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	FComposableCameraPose PreviousSourcePose;

	/** Delta time of the frame when the transition was created. */
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	float DeltaTime { 0.f };
};

/**
 * Base class for transition evaluation.
 *
 * Transitions are pose-only operators: they receive source and target poses each tick,
 * maintain their own internal blend state, and output a blended pose.
 * They never reference cameras or Directors directly.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionBase
	: public UObject
{
	GENERATED_BODY()

public:
	/** Evaluate the transition for this frame, blending between source and target poses. */
	FComposableCameraPose Evaluate(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose);

	/** Initialize the transition with source pose data. Called once before the first Evaluate. */
	void TransitionEnabled(const FComposableCameraTransitionInitParams& InInitParams);

	/** Mark the transition as finished. */
	void TransitionFinished();

	void SetTransitionTime(float NewTransitionTime);
	void ResetTransitionState();

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetRemainingTime() const { return RemainingTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetTransitionTime() const { return TransitionTime; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Transition")
	float GetPercentage() const { return Percentage; }

	/**
	 * Evaluate the transition's timing curve at a given normalized progress
	 * in [0, 1]. Returns the blend weight this transition would apply at
	 * that progress. I.e. the shape of its Percentage-over-duration curve.
	 *
	 * Used exclusively by the debug panel to render a sparkline preview of
	 * the blend curve on top of the transition's progress bar. The call
	 * site is a one-per-frame-per-active-transition sample loop, so the
	 * implementation must stay cheap (no allocations, no state reads that
	 * mutate). NOT used on the runtime evaluation hot path. Real blend
	 * weight is still derived from `Percentage` in `OnEvaluate` so that
	 * per-transition state (polynomials, curves, etc.) keeps driving it.
	 *
	 * Default implementation returns the input unchanged, giving a linear
	 * diagonal. The right fallback for transitions whose concept of
	 * "blend weight" isn't a simple scalar of normalized time (Inertialized
	 * position path, for example, is a polynomial trajectory, not a scalar
	 * lerp; showing a diagonal there still reads as "progress = time" which
	 * is accurate for its rotational / overall progression).
	 *
	 * Concrete overrides should be pure math. No reads of `RemainingTime`,
	 * `TransitionTime`, or any internal state. Use only the `NormalizedTime`
	 * argument plus the transition's authored UPROPERTYs (Exp, bSmootherStep,
	 * EvaluationCurveType, etc.).
	 */
	virtual float GetBlendWeightAt(float NormalizedTime) const
	{
		return FMath::Clamp(NormalizedTime, 0.f, 1.f);
	}

protected:
	/**
	 * Blend two poses while keeping the rotation path chosen when this transition started.
	 *
	 * Source and target poses are still evaluated live every frame. Any rotation
	 * movement that happens after the transition starts is accumulated as an
	 * endpoint offset and faded out/in by the same blend weight. This prevents
	 * mid-transition live input on only one side from making the blend re-pick a
	 * different shortest path.
	 */
	FComposableCameraPose BlendPosesByLockedRotationPath(
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose,
		float TargetWeight) const;

	/** Evaluate only the locked rotation path plus live endpoint offsets. */
	FRotator BlendRotationByLockedPath(float TargetWeight) const;

	/** Apply live endpoint rotation offsets to a transition-specific base rotation. */
	FRotator ApplyLiveRotationOffsetsToBaseRotation(
		const FRotator& BaseRotation,
		float TargetWeight) const;

	const FRotator& GetInitialTargetRotation() const { return InitialTargetRotation; }

	/** Begin Play event. Called on the first frame of the transition, before the first OnEvaluate. \n
	 * Use this to construct or initialize internal parameters specialized for this type of transition. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentSourcePose Current source camera pose. \n
	 * @param CurrentTargetPose Current target camera pose. \n
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnBeginPlay", Category = "ComposableCameraSystem|Transition")
	void OnBeginPlay(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose);
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) {}

	/** Event to customize the evaluation function for each tick. When calling this function, RemainingTime has already been decremented, and assured to not go below 0. \n
	 * @param DeltaTime World delta time. \n
	 * @param CurrentSourcePose Current source camera pose. \n
	 * @param CurrentTargetPose Current target camera pose. \n
	 * @return Returns the new blended camera pose.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnTick", Category = "ComposableCameraSystem|Transition")
	FComposableCameraPose OnEvaluate(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose);
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) { return FComposableCameraPose{}; }

	/**
	 * Event when the transition finishes. The base class simply sets bFinished to true.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnTransitionFinish"), Category = "ComposableCameraSystem|Transition")
	void OnFinished();

public:
	FOnTransitionFinishes OnTransitionFinishesDelegate;

#if !UE_BUILD_SHIPPING
public:
	/**
	 * Per-frame pose snapshot captured for debug visualization.
	 *
	 * Deliberately narrower than `FComposableCameraPose`. We only keep the
	 * fields `DrawStandardTransitionDebug` actually reads (position,
	 * rotation, resolved FOV in degrees). Skipping `FPostProcessSettings`
	 * matters: that struct embeds `TObjectPtr<UTexture>` / similar UObject
	 * references through its color-grading / vignette / bloom sub-structs,
	 * and our cache is NOT a UPROPERTY, so those references wouldn't be
	 * tracked by the GC. Caching only POD-like fields sidesteps the issue
	 * entirely AND shrinks each transition's per-frame debug memory from
	 * ~3x sizeof(FPostProcessSettings) to a few dozen bytes.
	 */
	struct FTransitionDebugSnapshot
	{
		FVector  Position   { FVector::ZeroVector };
		FRotator Rotation   { FRotator::ZeroRotator };
		float    FOVDegrees { 90.f };
	};

	// --- Debug: per-frame pose cache --------------------------------------
	//
	// Written every frame by `FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate`
	// (the single place where source / target / blended are all visible
	// simultaneously). Consumed by DrawTransitionDebug overrides through
	// the DrawStandardTransitionDebug helper.
	//
	// Compiled out in shipping builds together with the whole debug path.
	FTransitionDebugSnapshot LastDebugSource;
	FTransitionDebugSnapshot LastDebugTarget;
	FTransitionDebugSnapshot LastDebugBlended;

	/**
	 * Per-transition world-space debug hook.
	 *
	 * Invoked from `UComposableCameraEvaluationTree::DrawTransitionsDebug`
	 * while `CCS.Debug.Viewport` is on, once per frame for every transition
	 * that currently sits in an Inner node of the active director's tree
	 * (and, recursively, of any referenced director's tree. Inter-context
	 * blends see both sides).
	 *
	 * Default implementation is empty. Concrete transitions override, check
	 * their own `CCS.Debug.Viewport.Transitions.<Name>` CVar, and usually
	 * call `DrawStandardTransitionDebug` plus any type-specific extras
	 * (spline curve sample, feeler rays, etc.).
	 *
	 * @param World                 World to draw into. Routes through the
	 *                              world's LineBatcher, so the draw is
	 *                              visible in every viewport that renders
	 *                              this world (game + F8-ejected editor).
	 * @param bViewerIsOutsideCamera True when the player is NOT viewing
	 *                              through the camera (F8 eject / SIE).
	 *                              Overrides use this to skip gizmos that
	 *                              would occlude the near plane. Mostly
	 *                              the source/target frustum pyramids.
	 *
	 * Compiled out in shipping builds.
	 */
	virtual void DrawTransitionDebug(class UWorld* World, bool bViewerIsOutsideCamera) const {}

protected:
	/**
	 * Shared helper that paints the canonical source / target / progress
	 * endpoint markers. Each concrete transition's override still needs to
	 * draw its OWN path polyline (straight line, arc, polynomial, spline,
	 * rail, etc.) on top of these markers. The helper is deliberately
	 * silent about path shape because that's per-transition-type.
	 *
	 * Always drawn (possessed play + F8 eject):
	 *   - Green sphere at LastDebugSource.Position  (r = 15)
	 *   - Blue sphere at LastDebugTarget.Position    (r = 15)
	 *   - AccentColor sphere at LastDebugBlended.Position (r = 20) - the actual camera position this frame. For non-linear transitions
	 *     (Spline, Cylindrical, Inertialized, PathGuided) this will visibly
	 *     sit off the straight source-to-target axis.
	 *
	 * F8 / SIE only (drawn when bViewerIsOutsideCamera is true):
	 *   - Half-scale green frustum at the source pose.
	 *   - Half-scale blue frustum at the target pose.
	 *
	 * Frustums are intentionally skipped in possessed play: the blended
	 * frustum is already drawn by the camera-level frustum path, and
	 * source/target frustums would pile up against the near plane.
	 *
	 * AccentColor should be distinct from every node-gizmo color in the
	 * codebase (see `Docs/TechDoc.md Section 3.20.4` for the reserved-color table).
	 */
	void DrawStandardTransitionDebug(class UWorld* World, bool bViewerIsOutsideCamera, const FColor& AccentColor) const;
#endif // !UE_BUILD_SHIPPING

protected:
	// Transition time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	float TransitionTime;

	// Remaining transition time.
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	float RemainingTime;

	// If finished transition.
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	bool bFinished { false };

	// If at the first frame of transition.
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	bool bFirstFrame { true };

	// Initialization parameters from TransitionEnabled.
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	FComposableCameraTransitionInitParams InitParams;

	// How much percentage this transition has completed.
	UPROPERTY(BlueprintReadOnly, Category = "Transition")
	float Percentage { 0.f };

	// Rotation path state captured at transition start. Not UPROPERTY: transient
	// hot-path math state derived from evaluated poses, not authored data.
	FRotator InitialSourceRotation { FRotator::ZeroRotator };
	FRotator InitialTargetRotation { FRotator::ZeroRotator };
	FRotator InitialRotationDelta { FRotator::ZeroRotator };
	FRotator PreviousSourceRotation { FRotator::ZeroRotator };
	FRotator PreviousTargetRotation { FRotator::ZeroRotator };
	FRotator AccumulatedSourceRotationOffset { FRotator::ZeroRotator };
	FRotator AccumulatedTargetRotationOffset { FRotator::ZeroRotator };
	bool bHasLockedRotationPathState { false };

	/** Cached `GetClass()->GetName()` populated lazily at first Evaluate
	 *  and reused by per-evaluate `TRACE_CPUPROFILER_EVENT_SCOPE_STR` so
	 *  the dynamic Insights label doesn't allocate an FString per
	 *  evaluation. The class is per-instance immutable after construction,
	 *  so the lazy populate runs once over the transition's lifetime.
	 *  Non-UPROPERTY because it's transient diagnostic state, not data. */
	FString TransitionClassTraceName;

private:
	void InitializeLockedRotationPathState(
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose);

	void UpdateLockedRotationPathState(
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose);
};
