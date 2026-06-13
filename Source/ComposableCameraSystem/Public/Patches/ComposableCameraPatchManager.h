// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "ComposableCameraPatchManager.generated.h"

class UComposableCameraPatchTypeAsset;
class UComposableCameraPatchHandle;
class UComposableCameraPatchInstance;

/**
 * Director-scoped owner of active Camera Patches.
 *
 * Constructed by UComposableCameraDirector and destroyed with it. Apply runs
 * after the director evaluation tree, advances patch envelopes, checks
 * expiration, ticks each patch evaluator with the current upstream pose, and
 * blends outputs in layer order.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPatchManager : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraPatchManager();

	/**
	 * Add a Patch on this Director.
	 *
	 * Rejects null assets and Duration-enabled patches whose resolved Duration
	 * is <= 0. Logs a warning and returns nullptr on rejection.
	 *
	 * On success, spawns the evaluator camera, builds it from the patch asset,
	 * sorted-inserts the instance into ActivePatches by (LayerIndex ascending,
	 * PushSequence ascending), and returns a handle.
	 *
	 * @param ParameterBlock  Caller-supplied exposed-parameter / exposed-variable
	 *                        values for the Patch evaluator. Same shape and
	 *                        keyspace as the block accepted by
	 *                        ActivateNewCameraFromTypeAsset; routed through the
	 *                        Patch evaluator's ConstructCameraFromTypeAsset and
	 *                        cached on the runtime instance for later
	 *                        re-construction.
	 */
	UComposableCameraPatchHandle* AddPatch(
		UComposableCameraPatchTypeAsset* PatchAsset,
		const FComposableCameraPatchActivateParams& Params,
		const FComposableCameraParameterBlock& ParameterBlock);

	/**
	 * Manually retire a Patch.
	 *
	 * Live patches flip to Exiting and are removed by Apply after the exit
	 * envelope reaches Expired.
	 *
	 * @param ExitDurationOverride < 0 ->use the Patch's own ExitDuration; 0 ->cut immediately.
	 */
	void ExpirePatch(UComposableCameraPatchHandle* Handle, float ExitDurationOverride = -1.f);

	/**
	 * Per-frame application pass.
	 *
	 * Iterates sorted ActivePatches. Each non-expired patch sees the pose
	 * produced by the tree plus all lower-layer patches, then blends its
	 * evaluator output by CurrentAlpha.
	 */
	[[nodiscard]] FComposableCameraPose Apply(
		float DeltaTime, const FComposableCameraPose& InputPose);

	/**
	 * Synchronous teardown of every active Patch. Called when the owning context
	 * is popped immediately (non-top context) or the Director itself is being
	 * destroyed. Patches in flight do NOT get an exit blend through this path - matching how EvaluationTree is dropped synchronously in the same situations.
	 */
	void DestroyAll();

	/**
	 * Soft "expire every active Patch". Flips each one to Exiting via the
	 * normal envelope ramp (mirroring per-handle ExpirePatch). Patches mid-Entering
	 * fade out from their current alpha; Active-phase patches fade from 1.
	 * Already-Exiting / Expired entries are left alone (idempotent). Removal
	 * happens in the next Apply pass's end-of-frame sweep, NOT inside this call.
	 *
	 * @param ExitDurationOverride < 0 ->each patch keeps its own ExitDuration.
	 *                             >= 0 ->that value replaces every patch's ExitDuration.
	 */
	void ExpireAll(float ExitDurationOverride = -1.f);

	/**
	 * Mid-life parameter mutation. Re-applies a parameter block onto the
	 * Patch evaluator's runtime data block via the source asset's
	 * ApplyParameterBlock. Exactly the path the LS Component uses on its
	 * own per-tick re-sync (see UComposableCameraLevelSequenceComponent::TickComponent).
	 *
	 * Drives Sequencer integration's per-frame parameter keying: the patch
	 * track's TrackInstance::OnAnimate rebuilds a block from the section's
	 * current bag values and pushes it through this entry once per frame
	 * for every still-active section.
	 *
	 * Cost is O(exposed-parameter count) per call; ApplyParameterBlock copies
	 * typed values directly into the data block with no allocations on hits.
	 * Safe to call every frame on the same handle. No-op if the handle is
	 * null / stale / already in Exiting / Expired phase, or if the evaluator
	 * has no runtime data block yet.
	 *
	 * NOTE: this is NOT the broader `SetPatchParameter(handle, name, value)`
	 * runtime mutation API. It takes a complete parameter block (every
	 * exposed value at once), which is the natural shape Sequencer keys produce.
	 * A single-key per-call API is still deferred (PatchSystemProposal Section 0
	 * "Remaining deferred work") because there is no current driver for it.
	 */
	void ApplyParameterBlockToActivePatch(
		UComposableCameraPatchHandle* Handle,
		const FComposableCameraParameterBlock& Parameters);

	int32 GetActivePatchCount() const { return ActivePatches.Num(); }

	const TArray<TObjectPtr<UComposableCameraPatchInstance>>& GetActivePatches() const
	{
		return ActivePatches;
	}

	/** Capture the current ActivePatches array as a value-type snapshot consumed by
	 *  the debug HUD / dump commands. Walks ActivePatches in iteration order
	 *  (sorted by LayerIndex asc, PushSequence asc. Same order Apply uses). */
	void BuildDebugSnapshot(TArray<struct FComposableCameraPatchSnapshot>& OutPatches) const;

private:
	/** Sorted by (LayerIndex ascending, PushSequence ascending). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UComposableCameraPatchInstance>> ActivePatches;

	/** Monotonic counter. Assigned to each new instance's PushSequence on insert. */
	UPROPERTY(Transient)
	int32 NextPushSequence = 0;
};
