// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTargetInfo.generated.h"

class AActor;

/**
 * Identifies a single world-frame point on (or near) an Actor — the "pivot"
 * that camera nodes resolve to a world position each frame.
 *
 * Phase A consumers (V1.x): FocusPullNode, OcclusionFadeNode — both call
 * ResolveWorldPoint() instead of duplicating the same bone-walk-and-fallback
 * logic inline. The other two pivot-using nodes (CollisionPushNode,
 * ScreenSpacePivotNode) keep their own resolution code in V1: CollisionPush
 * caches a SkeletalMeshComponent on activation for hot-path speed;
 * ScreenSpacePivot has no bone path at all (it's just ActorLocation +
 * WorldUpOffset). They may adopt this struct later if and when there's a
 * win.
 *
 * Phase B+ consumers: the Shot composition system (see
 * Docs/ShotBasedKeyframing.md) — used directly inside FShotTarget and
 * resolved via the Composition Solver.
 *
 * Resolution proceeds in two paths:
 *
 *   1. Bone path (bUseBoneAsPivot && BoneName valid): walk Actor's
 *      SkeletalMeshComponents, pick the first whose skeleton has a socket
 *      / bone named BoneName. Pivot base = socket world location, pivot
 *      frame rotation = socket world quaternion.
 *
 *   2. Actor path (bUseBoneAsPivot is false, OR the bone path failed):
 *      pivot base = Actor->GetActorLocation(), pivot frame rotation =
 *      Actor->GetActorQuat().
 *
 * Then Offset is added on top: in world space if bOffsetInLocalSpace is
 * false (default — matches the legacy "world Z offset" semantics that
 * existing pivot-using nodes pass through), or rotated through the pivot
 * frame quaternion first if bOffsetInLocalSpace is true.
 *
 * Properties are BlueprintReadOnly per Docs/ShotBasedKeyframing.md §1.4 —
 * Shot data is designer-authored content, not gameplay-controlled state.
 * The struct is BlueprintType for editor / Sequencer / Details panel
 * reflection only.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraTargetInfo
{
	GENERATED_BODY()

	/**
	 * The actor whose pivot this struct describes. Soft-referenced so the
	 * Details-panel actor picker can span LEVEL actors from any world — the
	 * containing UAsset (Camera Type Asset, future Shot Asset) is not bound
	 * to a specific Level, so a hard `TObjectPtr<AActor>` would only allow
	 * picking persistent / package-scoped actors and silently fail for
	 * level-instance actors. `TSoftObjectPtr` matches the workflow used by
	 * BlackEyeCameras' `FBlackEyeSimpleTarget::Actor` for the same reason.
	 *
	 * Resolution to a live `AActor*` happens lazily via `Actor.Get()` inside
	 * `ResolveWorldPoint` — returns the loaded actor if currently in
	 * memory, nullptr otherwise (no force-load on the hot path).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Target)
	TSoftObjectPtr<AActor> Actor;

	/** When true, BoneName resolves on Actor's skeletal mesh and that bone
	 *  / socket location is used as the pivot base. When false, Actor's
	 *  location is used. Offset is added on top of either. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Target)
	bool bUseBoneAsPivot = false;

	/** Bone or socket on Actor's skeletal mesh. Used iff bUseBoneAsPivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Target,
		meta = (EditCondition = "bUseBoneAsPivot"))
	FName BoneName;

	/** Vector offset added to whichever pivot base is resolved. Default
	 *  zero reproduces "no offset"; (0, 0, Z) reproduces the legacy
	 *  "world-Z offset" pattern that existing pivot-using nodes pass
	 *  through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Target)
	FVector Offset = FVector::ZeroVector;

	/** When true, Offset is rotated through the resolved pivot's local
	 *  frame before being added — bone's socket quaternion in bone path,
	 *  actor's quaternion in actor path. When false (default), Offset is
	 *  added directly in world space — matches the legacy "PivotZOffset"
	 *  semantics that existing FocusPullNode / OcclusionFadeNode authors
	 *  expect when their call sites construct an FComposableCameraTargetInfo
	 *  via the Phase A migration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Target)
	bool bOffsetInLocalSpace = false;

	/**
	 * When true, AND the resolved Actor has at least one SkeletalMeshComponent,
	 * the **anchor basis** the Composition Solver uses for camera-relative
	 * placement (Shot's `Placement.BasisFrame == InheritFromActor` mode)
	 * is read from the SkelMesh component's world quaternion instead of the
	 * Actor's quaternion.
	 *
	 * Why this exists: UE's `ACharacter` defaults the `Mesh`
	 * SkeletalMeshComponent to `RelativeRotation = (0, -90, 0)` so the
	 * DCC-authored character (typically faces local +Y or -Y by Maya / Max
	 * convention) visually faces the actor's world +X (the gameplay forward
	 * axis used by `GetActorForwardVector`, MovementComponent, AI). With
	 * this flag false, `LocalCameraDirection=(0,0)` places the camera along
	 * "actor +X" — correct relative to gameplay forward, but offset by 90°
	 * relative to the **visible** mesh forward (which faces actor +Y). With
	 * this flag true, the basis is read from the mesh component's world quat
	 * — `LocalCameraDirection=(0,0)` then places the camera in front of the
	 * visual forward, matching designer expectation for character-style
	 * subjects.
	 *
	 * Has no effect when:
	 *   - Shot's `Placement.BasisFrame == World` (basis is forced identity).
	 *   - This Target isn't the SingleTarget anchor (basis isn't queried).
	 *   - Resolved Actor has no SkeletalMeshComponent (silently falls back
	 *     to the actor quat — same as if the flag were false).
	 *
	 * Default `false` to preserve V1.x behavior for existing assets. Toggle
	 * to true for Character-style targets when authoring new Shots.
	 *
	 * Independent of `bUseBoneAsPivot` / `BoneName`: this flag drives the
	 * BASIS for camera placement; those drive the PIVOT POINT for the
	 * camera to look at. Different concerns, separately authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Target)
	bool bUseSkeletalMeshForwardAsBasis = false;

	/**
	 * Resolves to a single world-frame point following the path described
	 * in the struct comment. Returns false when Actor is null; OutPoint is
	 * left unchanged on failure so callers can pre-seed it.
	 *
	 * Bone-path failure (bUseBoneAsPivot true but no SkeletalMeshComponent
	 * on Actor has the requested socket / bone) is silent: falls through
	 * to the actor path and still returns true. Same fallback the inline
	 * implementations had before consolidation.
	 *
	 * @param OutPoint     Resolved world-frame point. Left unchanged on
	 *                     failure (Actor null).
	 * @param OutUsedBone  Optional. When non-null, set to true iff the bone
	 *                     path resolved successfully (bUseBoneAsPivot was
	 *                     true AND BoneName was found on a SkelMesh). Set
	 *                     to false in every other case (no bone mode,
	 *                     fallback fired, or Actor null). Used by the V1.x
	 *                     migration call sites in FocusPullNode /
	 *                     OcclusionFadeNode to preserve the legacy "Z
	 *                     offset is applied only when the bone path did
	 *                     NOT resolve" semantic — see the call sites in
	 *                     those nodes for the exact pattern.
	 *
	 *                     Composition Solver / Shot data callers typically
	 *                     pass nullptr (default) — they treat Offset as
	 *                     always-applied regardless of which resolution
	 *                     path fired, which is the cleaner contract for
	 *                     forward-looking authored content.
	 */
	bool ResolveWorldPoint(FVector& OutPoint, bool* OutUsedBone = nullptr) const;

	/**
	 * Resolves the basis quaternion this target contributes to the
	 * Composition Solver's `ResolvePlacementBasis` step (when the Shot's
	 * `Placement.BasisFrame == InheritFromActor` and this target is
	 * referenced by `Placement.BasisActorIndex`). See
	 * `ComposableCameraShotSolver.h` for how the result enters the
	 * `LocalCameraDirection` rotation chain.
	 *
	 * Path:
	 *   1. Soft-resolve `Actor.Get()` and PIE-remap (same path as
	 *      `ResolveWorldPoint` — captures live PIE-instance quat instead
	 *      of editor-world authoring quat).
	 *   2. If `bUseSkeletalMeshForwardAsBasis` AND the actor has at least
	 *      one `USkeletalMeshComponent` → return that component's world
	 *      quat (`GetComponentQuat()`). For UE `ACharacter` this picks up
	 *      the conventional `(0, -90, 0)` mesh offset that aligns visual
	 *      forward to actor +X.
	 *   3. Otherwise → return `Actor->GetActorQuat()`.
	 *
	 * Returns false when the actor is null (`OutQuat` left unchanged so
	 * callers can pre-seed it to `FQuat::Identity`).
	 */
	bool ResolveBasisQuat(FQuat& OutQuat) const;
};
