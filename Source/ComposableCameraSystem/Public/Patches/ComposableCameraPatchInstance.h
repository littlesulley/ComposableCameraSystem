// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "ComposableCameraPatchInstance.generated.h"

class AComposableCameraCameraBase;
class UComposableCameraPatchTypeAsset;
class UComposableCameraPatchHandle;

/**
 * Runtime per-Patch state, owned by UComposableCameraPatchManager.
 *
 * Holds the source asset reference, the Patch evaluator camera actor (Stage 2+),
 * resolved layer / push-sequence for ordering, schedule fields, envelope state,
 * cached parameter block for evaluator (re)construction, and a back-link to the
 * user-facing handle.
 *
 * Stage 1 note: Evaluator stays nullptr; PatchManager::Apply does not tick anything
 * yet. The envelope state is populated to "Entering, alpha 0" at construction but
 * is not advanced — Stage 3 adds AdvanceEnvelope. All bookkeeping fields are valid
 * as soon as AddPatch returns so debug HUD / introspection work end-to-end.
 */
UCLASS()
class COMPOSABLECAMERASYSTEM_API UComposableCameraPatchInstance : public UObject
{
	GENERATED_BODY()

public:
	/** Source patch asset.
	 *
	 *  STRONG ref by design (not weak). The schedule's Condition channel —
	 *  one of the two ways a Patch normally exits — calls
	 *  `Asset->CanRemain(...)` every Apply tick to decide whether to flip
	 *  to Exiting. A weak ref that nullifies (asset only loaded
	 *  transiently — soft path, DataTable row, BP local that fell out of
	 *  scope) silently disables the Condition check in
	 *  CheckPatchScheduleExpiration and, for a Patch whose ONLY exit
	 *  channel is Condition, leaves the instance live in `ActivePatches`
	 *  forever (and the spawned Evaluator actor with it). Strong ref
	 *  keeps the asset reachable for the instance's lifetime, which the
	 *  instance was always going to need anyway — `Apply` reads
	 *  `Asset->Layer / Duration / Envelope...` on every tick, so a "weak
	 *  ref + survive cleanly when null" model would have to either
	 *  early-expire or no-op every Apply call, both of which are user-
	 *  visible regressions worse than the strong-ref cost. */
	UPROPERTY()
	TObjectPtr<UComposableCameraPatchTypeAsset> SourcePatchAsset;

	/** Cached `Asset->GetName()` populated at AddPatch time. Reused by the
	 *  per-Apply `TRACE_CPUPROFILER_EVENT_SCOPE_STR` so the dynamic Insights
	 *  label doesn't allocate an FString per patch per frame. The asset
	 *  identity is immutable for the lifetime of the instance, so we
	 *  compute it once when the instance is constructed. */
	FString PatchAssetTraceName;

	/** The Patch's own camera-actor evaluator. Stage 2+: spawned by AddPatch via
	 *  UE::ComposableCameras::ConstructCameraFromTypeAsset. Stage 1: always nullptr. */
	UPROPERTY()
	TObjectPtr<AComposableCameraCameraBase> Evaluator;

	/** Effective composition order. Resolved from the asset default and the per-AddPatch
	 *  override (Params.bOverrideLayerIndex true → use Params.LayerIndex). */
	UPROPERTY()
	int32 LayerIndex = 0;

	/** Monotonic insertion sequence — tiebreaker for equal LayerIndex. Older first. */
	UPROPERTY()
	int32 PushSequence = 0;

	// ─── Schedule ──────────────────────────────────────────────────────────

	/** Bitmask of EComposableCameraPatchExpirationType channels that may fire. */
	UPROPERTY()
	uint8 ExpirationType = 0;

	UPROPERTY()
	float Duration = 0.f;

	UPROPERTY()
	bool bExpireOnCameraChange = false;

	// ─── Envelope ──────────────────────────────────────────────────────────

	UPROPERTY()
	float EnterDuration = 0.f;

	UPROPERTY()
	float ExitDuration = 0.f;

	UPROPERTY()
	EComposableCameraPatchEase EaseType = EComposableCameraPatchEase::EaseInOut;

	UPROPERTY()
	EComposableCameraPatchPhase Phase = EComposableCameraPatchPhase::Entering;

	/** Time spent in the current Phase. Reset to 0 on every phase transition. */
	UPROPERTY()
	float ElapsedInPhase = 0.f;

	/** Cumulative time spent in the Active phase (used by the Duration channel). */
	UPROPERTY()
	float ElapsedTimeActive = 0.f;

	UPROPERTY()
	float CurrentAlpha = 0.f;

	/** The CurrentAlpha at the moment Phase flipped to Exiting. The exit ramp
	 *  scales the eased curve by this value so a Patch retired mid-Entering
	 *  fades out from wherever it had reached, instead of popping to 1 first.
	 *  Stays at 1 by default for the common case (Active → Exiting transition). */
	UPROPERTY()
	float ExitStartAlpha = 1.f;

	// ─── Construction Inputs ───────────────────────────────────────────────

	/** Cached parameter block from AddPatch. Used by Stage 2's
	 *  ConstructCameraFromTypeAsset call and any future re-construction
	 *  (e.g. in response to modifier changes). */
	UPROPERTY()
	FComposableCameraParameterBlock CachedParameters;

	/** RunningCamera observed on the owning Director at AddPatch time. When
	 *  bExpireOnCameraChange is true, the schedule check compares this against
	 *  the Director's current RunningCamera each frame and flips the Patch to
	 *  Exiting if they differ (per-patch tracking — a Patch born during camera
	 *  A never treats its own birth as a "change"). */
	UPROPERTY()
	TWeakObjectPtr<AComposableCameraCameraBase> RunningCameraAtAdd;

	// ─── Handle Back-Link ──────────────────────────────────────────────────

	/** Back-link to the user-facing handle. Weak — the handle can be released by
	 *  the caller while the instance is still alive (the instance keeps running
	 *  until expiration; the caller has just opted out of further handle queries). */
	UPROPERTY()
	TWeakObjectPtr<UComposableCameraPatchHandle> Handle;
};
