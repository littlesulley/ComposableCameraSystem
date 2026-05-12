// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCompositionFramingNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
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

// Per-process registry consulted by the LS / PIE viewport overlay.
// Static linkage; only the `GetActiveInstances` accessor is exposed.
// Lifetime: bound by individual OnInitialize / BeginDestroy hooks.
TSet<TWeakObjectPtr<UComposableCameraCompositionFramingNode>>
GComposableCameraFramingActiveSet;

const TSet<TWeakObjectPtr<UComposableCameraCompositionFramingNode>>&
UComposableCameraCompositionFramingNode::GetActiveInstances()
{
	return GComposableCameraFramingActiveSet;
}

void UComposableCameraCompositionFramingNode::BeginDestroy()
{
	GComposableCameraFramingActiveSet.Remove(this);
	Super::BeginDestroy();
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

#if !UE_BUILD_SHIPPING
	// Join the LS-overlay registry. Idempotent — `TSet` ignores duplicate
	// inserts. Removed in BeginDestroy.
	GComposableCameraFramingActiveSet.Add(this);
#endif

	// Drop any stale framing-zone prior-pose state — the next OnTickNode
	// will V1-hard-seed a fresh pose for whichever Shot is active. Required
	// because OnInitialize fires on every camera reactivation; carrying an
	// old prior pose across a deactivate / reactivate cycle would project
	// the anchor against a pose whose context (target list, screen position,
	// FOV authoring) may have shifted, producing a one-frame visible snap
	// even when the new shot's hard solve would have landed exactly where
	// the previous activation ended.
	bHasLastPrimaryOutputPose   = false;
	bHasLastSecondaryOutputPose = false;
	LastPrimaryDistance         = -1.f;
	LastSecondaryDistance       = -1.f;
	LastPrimaryFOV              = -1.f;
	LastSecondaryFOV            = -1.f;
	LastPrimaryRoll             = TNumericLimits<float>::Max();
	LastSecondaryRoll           = TNumericLimits<float>::Max();
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
	float DeltaTime,
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

	// Compose the prior-pose snapshot for zone preprocessing. Passing
	// `nullptr` to SolveShot disables zones for that pass — used on the
	// first tick after activation/reseed, when no valid prior pose exists
	// yet and the solver should V1-hard-seed the cache instead.
	FShotPriorPose PrimaryPrior;
	const FShotPriorPose* PrimaryPriorPtr = nullptr;
	if (bHasLastPrimaryOutputPose)
	{
		PrimaryPrior.Position     = LastPrimaryOutputPosition;
		PrimaryPrior.Rotation     = LastPrimaryOutputRotation;
		PrimaryPrior.LastDistance = LastPrimaryDistance;
		PrimaryPrior.LastFOV      = LastPrimaryFOV;
		PrimaryPrior.LastRoll     = LastPrimaryRoll;
		PrimaryPriorPtr           = &PrimaryPrior;
	}

	// ─── 3. Primary solver pass ──────────────────────────────────────
	const FShotSolveResult PrimaryResult = SolveShot(Shot, Context, PrimaryPriorPtr, DeltaTime);

	if (!PrimaryResult.bValid)
	{
		// Primary anchor unresolvable. Solver already logged a generic
		// warning; emit a richer one (gated to once-per-owner-per-state-
		// change) so the failing Shot can be identified without a debugger
		// attach. Tracks the prior printed state per node so a steady-state
		// failure logs once instead of every frame, and recovery (anchor
		// becomes resolvable again) re-arms the warn for the next failure.
		const bool bShouldEmitDetailedWarn =
			!bLastTickWasUnresolved
			|| LastUnresolvedTargetCount != Shot.Targets.Num();
		if (bShouldEmitDetailedWarn)
		{
			const AActor* OwnerActor = OwningCamera ? OwningCamera->GetOwner() : nullptr;
			const AActor* OwnerActorOuter = OwnerActor ? OwnerActor->GetOwner() : nullptr;
			FString TargetsDump;
			if (Shot.Targets.Num() == 0)
			{
				TargetsDump = TEXT("Targets[] is EMPTY (Shot has no targets — SingleTarget anchor mode will fail with TargetIndex out of range)");
			}
			else
			{
				for (int32 i = 0; i < Shot.Targets.Num(); ++i)
				{
					const FComposableCameraTargetInfo& T = Shot.Targets[i].Target;
					AActor* Resolved = T.Actor.Get();
					const FString PathStr = T.Actor.ToSoftObjectPath().ToString();
					const UWorld* ResolvedWorld = Resolved ? Resolved->GetWorld() : nullptr;
					const TCHAR* WorldTypeStr = TEXT("<none>");
					if (ResolvedWorld)
					{
						switch (ResolvedWorld->WorldType)
						{
							case EWorldType::Editor:        WorldTypeStr = TEXT("Editor");        break;
							case EWorldType::EditorPreview: WorldTypeStr = TEXT("EditorPreview"); break;
							case EWorldType::PIE:           WorldTypeStr = TEXT("PIE");           break;
							case EWorldType::Game:          WorldTypeStr = TEXT("Game");          break;
							case EWorldType::GamePreview:   WorldTypeStr = TEXT("GamePreview");   break;
							default: break;
						}
					}
					TargetsDump += FString::Printf(
						TEXT("\n    Targets[%d]: SoftPath='%s' Actor.Get()=%s%s%s"),
						i,
						*PathStr,
						Resolved ? *Resolved->GetName() : TEXT("<null>"),
						Resolved ? *FString::Printf(TEXT(" (in %s world)"), WorldTypeStr) : TEXT(""),
						T.bUseBoneAsPivot ? *FString::Printf(TEXT(" Bone='%s'"), *T.BoneName.ToString()) : TEXT(""));
				}
			}

			const TCHAR* AnchorModeStr = TEXT("?");
			switch (Shot.Placement.PlacementAnchor.Mode)
			{
				case EShotAnchorMode::SingleTarget:           AnchorModeStr = TEXT("SingleTarget"); break;
				case EShotAnchorMode::WeightedWorldCentroid:  AnchorModeStr = TEXT("WeightedWorldCentroid"); break;
				case EShotAnchorMode::FixedWorldPosition:     AnchorModeStr = TEXT("FixedWorldPosition"); break;
			}

			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("CompositionFramingNode: Primary SolveShot FAILED on '%s' (outer='%s'). "
				     "PlacementAnchor.Mode=%s, TargetIndex=%d, Targets.Num()=%d.%s\n"
				     "  → Camera will fall back to upstream pose (default identity for fresh InternalCamera, last good pose otherwise). "
				     "If you see camera-at-origin in PIE: TargetActorOverrides binding likely failed to resolve in PIE — verify the Possessable's level-actor exists in the PIE world, or check Sequencer binding remap timing."),
				OwnerActor ? *OwnerActor->GetName() : TEXT("<no owner>"),
				OwnerActorOuter ? *OwnerActorOuter->GetName() : TEXT("<none>"),
				AnchorModeStr,
				Shot.Placement.PlacementAnchor.TargetIndex,
				Shot.Targets.Num(),
				*TargetsDump);
		}
		bLastTickWasUnresolved = true;
		LastUnresolvedTargetCount = Shot.Targets.Num();

		// Signal to the LS Component projection path: this tick produced no
		// valid framing pose, so don't write the upstream-default pose over
		// the CineCamera's current transform. Critical if evaluation runs
		// before the Shot TrackInstance pushes its first override and would
		// otherwise burn an origin pose
		// onto a freshly-spawned LSShotActor's CineCam, which then leaks
		// to the PCM ViewTarget POV and renders one frame at world origin.
		// With the flag set, ProjectPoseToCineCamera holds the CineCam's
		// last-valid transform; the second EvaluateOnce triggered by the
		// TrackInstance push (via SetSequencerShotOverride's first-entry
		// re-eval) writes the correct pose before the renderer captures.
		if (OwningCamera)
		{
			OwningCamera->bLastTickFramingFailed = true;
		}

		// Primary anchor unresolvable. Solver already logged a warning; pass
		// the upstream pose through unchanged (camera holds its previous
		// frame's state until the Shot becomes resolvable again). Phase F
		// secondary is intentionally NOT attempted in this fallback — the
		// V1 "hold last frame" semantic is preserved when the active
		// primary framing breaks. Designer can fix the Shot data.
		//
		// Prior-pose cache is intentionally NOT cleared here — when the
		// shot becomes resolvable again, projecting through the last
		// good pose is a better starting point for the zone math than a
		// hard-seed, because the user-visible camera position is still
		// holding at that pose.
		OutCameraPose = CurrentCameraPose;
		++LocalFrameCounter;
		return;
	}

	// Recovery — clear the unresolved-state tracker so the next failure
	// re-arms the detailed warn (don't accumulate stale state across runs).
	bLastTickWasUnresolved = false;
	LastUnresolvedTargetCount = INDEX_NONE;
	if (OwningCamera)
	{
		OwningCamera->bLastTickFramingFailed = false;
	}

	FComposableCameraPose PrimaryPose;
	ApplySolverResultToPose(PrimaryResult, CurrentCameraPose, PrimaryPose);

	// Cache the primary's solved pose for next-frame zone preprocessing.
	// Done unconditionally on bValid — even when zones are currently
	// disabled, a future authoring change to enable them (or a Phase F
	// second-shot blend) should have a valid prior pose immediately,
	// not require a one-frame seed.
	LastPrimaryOutputPosition   = PrimaryResult.CameraPosition;
	LastPrimaryOutputRotation   = PrimaryResult.CameraRotation;
	LastPrimaryDistance         = PrimaryResult.EffectiveDistance;
	LastPrimaryFOV              = PrimaryResult.FieldOfView;
	LastPrimaryRoll             = PrimaryResult.CameraRotation.Roll;
	bHasLastPrimaryOutputPose   = true;

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

		FShotPriorPose SecondaryPrior;
		const FShotPriorPose* SecondaryPriorPtr = nullptr;
		if (bHasLastSecondaryOutputPose)
		{
			SecondaryPrior.Position     = LastSecondaryOutputPosition;
			SecondaryPrior.Rotation     = LastSecondaryOutputRotation;
			SecondaryPrior.LastDistance = LastSecondaryDistance;
			SecondaryPrior.LastFOV      = LastSecondaryFOV;
			SecondaryPrior.LastRoll     = LastSecondaryRoll;
			SecondaryPriorPtr           = &SecondaryPrior;
		}

		const FShotSolveResult SecondaryResult = SolveShot(SecondaryShot, Context,
			SecondaryPriorPtr, DeltaTime);
		if (SecondaryResult.bValid)
		{
			FComposableCameraPose SecondaryPose;
			ApplySolverResultToPose(SecondaryResult, CurrentCameraPose, SecondaryPose);

			LastSecondaryOutputPosition   = SecondaryResult.CameraPosition;
			LastSecondaryOutputRotation   = SecondaryResult.CameraRotation;
			LastSecondaryDistance         = SecondaryResult.EffectiveDistance;
			LastSecondaryFOV              = SecondaryResult.FieldOfView;
			LastSecondaryRoll             = SecondaryResult.CameraRotation.Roll;
			bHasLastSecondaryOutputPose   = true;

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
	float InAlpha,
	bool bPrimaryChanged)
{
	const bool bWasInBlend  = bHasSecondaryShot;
	const bool bWillBeInBlend = (InSecondaryShot != nullptr && InTransition != nullptr);

	// Primary cache must reseed in two distinct cases:
	//
	//   1. **Leaving a blend** (was secondary-active, now isn't). The
	//      outgoing primary section has expired and the *new* primary is
	//      typically what the previous secondary was carrying. The
	//      framing node has no asset-identity hook to verify "is this
	//      the same shot that was the secondary last frame", so it
	//      can't promote `LastSecondaryOutputPose → LastPrimaryOutputPose`.
	//      Reseed the primary cache so the next OnTickNode V1-hard-seeds.
	//
	//   2. **Section A → B cut, no overlap** (`bPrimaryChanged` set by
	//      the LSComponent). Without reseeding, V2.2 damping (Distance /
	//      FOV / Roll) carries the previous shot's pose values into the
	//      new shot's first frame — the camera glides from Shot A's
	//      framing toward Shot B's authored values over the IIR window
	//      instead of cutting cleanly. Designer-visible regression
	//      reported during V2.2 polish: "进入 Shot 时有 damping 效果而
	//      不是一帧就到指定位置". Reseeding here makes cuts cuts.
	//
	// Designer-visible cost in either case: a single frame where the
	// anchor snaps into composition center instead of riding the prior
	// soft-zone trajectory — accepted in the design discussion as the
	// cost of preserving cut-as-cut semantics on Section transitions.
	if (bPrimaryChanged || (bWasInBlend && !bWillBeInBlend))
	{
		bHasLastPrimaryOutputPose = false;
		LastPrimaryDistance       = -1.f;
		LastPrimaryFOV            = -1.f;
		LastPrimaryRoll           = TNumericLimits<float>::Max();
	}

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

		// Drop the secondary prior-pose cache too — the next blend the
		// LSComponent triggers will likely be a *different* secondary Shot
		// than the one this cache was derived from, and projecting the new
		// shot's anchor through the old shot's pose would NaN the zone math
		// in the worst case (anchor behind camera) or just produce a one-
		// frame visible glitch in the best.
		bHasLastSecondaryOutputPose = false;
		LastSecondaryDistance       = -1.f;
		LastSecondaryFOV            = -1.f;
		LastSecondaryRoll           = TNumericLimits<float>::Max();
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
