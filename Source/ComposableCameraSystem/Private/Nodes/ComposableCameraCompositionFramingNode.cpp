// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCompositionFramingNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Math/ComposableCameraShotSolver.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraViewportUtils.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowCompositionFramingGizmo(
		TEXT("CCS.Debug.Viewport.CompositionFraming"),
		0,
		TEXT("Show CompositionFramingNode debug: orange sphere at the resolved\n")
		TEXT("anchor world position; small white spheres at each tracked\n")
		TEXT("Target's pivot. Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraCompositionFramingNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Seed bounds caches for all AutoFromComponentBounds targets — covers
	// StaticSnapshot policy entirely, and primes Periodic / Live so their
	// first OnTickNode pass uses fresh data instead of zero-extent.
	for (FComposableCameraShotTarget& T : Shot.Targets)
	{
		if (T.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds)
		{
			T.RefreshAutoBoundsCache();
		}
	}

	LocalFrameCounter = 0;
}

namespace
{
	using namespace ComposableCameraSystem::ShotSolver;

	/** Refresh the AutoFromComponentBounds cache on every applicable target
	 *  in `InOutShot` according to per-target policy + the shared frame
	 *  counter snapshot. Static helper so primary + Phase F secondary use
	 *  the same code path with identical phase alignment. */
	void RefreshShotBoundsCaches(FComposableCameraShot& InOutShot, int32 RefreshSnapshot)
	{
		for (FComposableCameraShotTarget& T : InOutShot.Targets)
		{
			if (T.BoundsShape != EShotTargetBoundsShape::AutoFromComponentBounds)
			{
				continue;
			}
			const bool bRefresh =
				T.BoundsCachePolicy == EBoundsCachePolicy::Live
				|| (T.BoundsCachePolicy == EBoundsCachePolicy::Periodic
					&& T.BoundsRefreshIntervalFrames > 0
					&& (RefreshSnapshot % T.BoundsRefreshIntervalFrames) == 0);
			if (bRefresh)
			{
				T.RefreshAutoBoundsCache();
			}
		}
	}

	/** Apply a successful `FShotSolveResult` onto `OutPose`, starting from
	 *  `UpstreamPose` so untouched fields persist. Same write set the V1
	 *  single-Shot path produced — Position / Rotation / FOV / sentinels
	 *  for the renderer's dual-mode FOV + DoF routing. */
	void ApplySolverResultToPose(
		const FShotSolveResult& Result,
		const FComposableCameraPose& UpstreamPose,
		FComposableCameraPose& OutPose)
	{
		OutPose = UpstreamPose;
		OutPose.Position                  = Result.CameraPosition;
		OutPose.Rotation                  = Result.CameraRotation;
		OutPose.FieldOfView               = Result.FieldOfView;
		OutPose.FocalLength               = -1.f;
		OutPose.Aperture                  = Result.Aperture;
		OutPose.FocusDistance             = Result.FocusDistance;
		OutPose.PhysicalCameraBlendWeight = 1.f;
	}
}

void UComposableCameraCompositionFramingNode::OnTickNode_Implementation(
	float /*DeltaTime*/,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	using namespace ComposableCameraSystem::ShotSolver;

	// ─── 1. Per-target bounds-cache refresh per policy ───────────────
	// Snapshot the counter so primary + Phase F secondary refreshes share
	// the same "this tick is N" decision. Counter advances once at the end.
	const int32 RefreshSnapshot = LocalFrameCounter;
	RefreshShotBoundsCaches(Shot, RefreshSnapshot);

	// ─── 2. Build solve context (shared between both solvers) ────────
	FShotSolveContext Context;

	// Aspect ratio resolves through the existing helper for the PCM-bound
	// path (TechDoc §3.18). The LS Component path pushes a CineCam-aware
	// override via `SetExternalAspectRatioOverride` each tick — that path
	// honors `bConstrainAspectRatio` (filmback letterbox) AND queries the
	// editor active viewport in scrub mode, both of which the bare
	// `GetEffectiveViewportAspectRatio` helper can't address from the
	// node's perspective (no CineCam reference here). When the override
	// is set (> 0) it wins; otherwise fall back.
	Context.ViewportAspectRatio = (ExternalAspectRatioOverride > 0.f)
		? ExternalAspectRatioOverride
		: UE::ComposableCameras::GetEffectiveViewportAspectRatio(OwningPlayerCameraManager);

	// PreviousFrameFOV: the upstream pose's FOV is the most recent committed
	// value (last frame's solve, or whatever upstream nodes wrote). Falls
	// back to primary Shot's ManualFOV / 79° when invalid (first tick edge
	// case where the pose is still at default sentinel).
	//
	// In Phase F's two-Shot blend both solvers see the same upstream FOV;
	// the previous-frame blended output is implicit in the upstream pose.
	const float UpstreamFOV = static_cast<float>(CurrentCameraPose.GetEffectiveFieldOfView());
	Context.PreviousFrameFOV =
		(UpstreamFOV > 0.f)
			? UpstreamFOV
			: ((Shot.Lens.FOVMode == EShotFOVMode::Manual) ? Shot.Lens.ManualFOV : 79.f);

	// ─── 3. Primary solver pass ──────────────────────────────────────
	const FShotSolveResult PrimaryResult = SolveShot(Shot, Context);

	if (!PrimaryResult.bValid)
	{
		// Primary anchor unresolvable. Solver already logged a warning; pass
		// the upstream pose through unchanged (camera holds its previous
		// frame's state until the Shot becomes resolvable again). Phase F
		// secondary is intentionally NOT attempted in this fallback — the
		// V1 "hold last frame" semantic is preserved when the active
		// primary framing breaks. Designer can fix the Shot data.
		OutCameraPose = CurrentCameraPose;
		++LocalFrameCounter;
		return;
	}

	FComposableCameraPose PrimaryPose;
	ApplySolverResultToPose(PrimaryResult, CurrentCameraPose, PrimaryPose);

	// ─── 4. Phase F secondary solver pass + transition pose blend ────
	// Active iff the LSComponent pushed both a secondary Shot AND a
	// resolved EnterTransition (decided in
	// `SetActiveShotsFromSequencer`). Inner `Transition` UPROPERTY may
	// still be null if the data asset was authored without a transition
	// instance — defensive null-check below.
	UComposableCameraTransitionBase* TransInst =
		(bHasSecondaryShot && ActiveBlendTransition)
			? ActiveBlendTransition->Transition
			: nullptr;

	if (TransInst)
	{
		// Same per-target bounds policy + same RefreshSnapshot as primary —
		// keeps periodic refresh phases aligned across both shots.
		RefreshShotBoundsCaches(SecondaryShot, RefreshSnapshot);

		const FShotSolveResult SecondaryResult = SolveShot(SecondaryShot, Context);
		if (SecondaryResult.bValid)
		{
			FComposableCameraPose SecondaryPose;
			ApplySolverResultToPose(SecondaryResult, CurrentCameraPose, SecondaryPose);

			// The transition asset's ease curve is the only contribution
			// taken from it — `GetBlendWeightAt` is a pure-math sample of
			// that curve at the overlap-window-driven NormalizedTime. The
			// transition's own time-based state (RemainingTime / Percentage)
			// is intentionally bypassed because the Section overlap window
			// is the authoritative duration source (spec §7.6 decision Q4).
			const float EasedAlpha = FMath::Clamp(
				TransInst->GetBlendWeightAt(ActiveBlendAlpha), 0.0f, 1.0f);

			OutCameraPose = PrimaryPose;
			OutCameraPose.BlendBy(SecondaryPose, EasedAlpha);
			++LocalFrameCounter;
			return;
		}
		// Secondary anchor unresolvable while primary resolved: fall through
		// to primary-only output. Avoids visible pop when the secondary
		// Shot has transient unresolved state (Spawnable not yet in world,
		// per-target binding override doesn't resolve, etc.). Designer
		// sees primary framing for the frame.
	}

	OutCameraPose = PrimaryPose;
	++LocalFrameCounter;
}

void UComposableCameraCompositionFramingNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& /*OutPins*/) const
{
	// V1: no pins. Shot is authored exclusively in the node's Details panel.
	// FComposableCameraShot contains TArray<FShotTarget> which violates the
	// pin data block's POD-only contract (TechDoc §3.2). Phase E LS Shot
	// Sections will push Shot updates via a separate runtime mutator API,
	// not via the pin system.
}

EComposableCameraNodePatchCompatibility
UComposableCameraCompositionFramingNode::GetPatchCompatibility_Implementation() const
{
	// This node OVERWRITES the pose by design — it doesn't read upstream
	// pose, it generates a fresh one from the authored Shot. Layering a
	// Patch on top has no defined semantics. Same classification as
	// RelativeFixedPoseNode / MixingCameraNode / ViewTargetProxyNode.
	return EComposableCameraNodePatchCompatibility::Incompatible;
}

void UComposableCameraCompositionFramingNode::SetActiveShotsFromSequencer(
	const FComposableCameraShot& InPrimaryShot,
	const FComposableCameraShot* InSecondaryShot,
	UComposableCameraTransitionDataAsset* InTransition,
	float InAlpha)
{
	Shot = InPrimaryShot;

	// Secondary state is active only when both a secondary Shot AND a
	// transition asset are provided. A null transition with a secondary
	// Shot collapses to V1 top-row-winner behavior: primary is the sole
	// solver input, secondary is silently ignored. This matches the
	// Phase F decision that null `EnterTransition` = hard cut, equivalent
	// to no blending at the section boundary (incoming snaps in only when
	// the outgoing's range ends and removes itself from the override map).
	if (InSecondaryShot && InTransition)
	{
		SecondaryShot         = *InSecondaryShot;
		bHasSecondaryShot     = true;
		ActiveBlendTransition = InTransition;
		ActiveBlendAlpha      = FMath::Clamp(InAlpha, 0.0f, 1.0f);
	}
	else
	{
		// Reset secondary state so OnTickNode (F.4) takes the single-Shot
		// path. Clearing the TObjectPtr also lets the asset GC if the
		// LSComponent has no other entry holding it.
		bHasSecondaryShot     = false;
		ActiveBlendTransition = nullptr;
		ActiveBlendAlpha      = 0.0f;
		// SecondaryShot's content is left as-is (cheap value type; will be
		// overwritten on the next overlap activation).
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraCompositionFramingNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowCompositionFramingGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos())
	{
		return;
	}

	constexpr uint8 KForeground = 1;

	// Orange sphere at the resolved Placement anchor; cyan sphere at the
	// resolved Aim anchor (when distinct). Re-resolves each frame so the
	// gizmo always shows the current world point even when the solver
	// decided to skip the pose update for the frame.
	FVector PlacementAnchorPos;
	if (Shot.Placement.PlacementAnchor.ResolveWorldPosition(Shot.Targets, PlacementAnchorPos))
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(
			World, PlacementAnchorPos, /*Radius=*/12.f, FColor(255, 130, 30),
			/*Alpha=*/120, /*Segments=*/16, KForeground);
	}
	FVector AimAnchorPos;
	if (Shot.Aim.AimAnchor.ResolveWorldPosition(Shot.Targets, AimAnchorPos))
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(
			World, AimAnchorPos, /*Radius=*/10.f, FColor(80, 200, 255),
			/*Alpha=*/120, /*Segments=*/16, KForeground);
	}

	// Smaller white spheres at each tracked Target's pivot — useful for
	// confirming "right actor + right bone + right offset" before the
	// solver sees them.
	for (const FComposableCameraShotTarget& T : Shot.Targets)
	{
		FVector Pivot;
		if (T.Target.ResolveWorldPoint(Pivot))
		{
			FComposableCameraViewportDebug::DrawSolidDebugSphere(
				World, Pivot, /*Radius=*/6.f, FColor::White,
				/*Alpha=*/100, /*Segments=*/12, KForeground);
		}
	}
}
#endif
