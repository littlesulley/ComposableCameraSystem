// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Engine/EngineTypes.h"
#include "Templates/SubclassOf.h"
#include "UObject/WeakObjectPtr.h"
#include "WorldCollision.h"
#include "ComposableCameraOcclusionFadeNode.generated.h"

class UMaterialInterface;
class UPrimitiveComponent;

/**
 * Per-primitive record of materials replaced while the primitive is in the
 * faded set. Stored as a USTRUCT on the node so GC keeps the
 * original-materials array alive until we restore it, which avoids a
 * dangling-pointer risk the moment a mesh's only remaining reference to its
 * original material is this record.
 *
 * OverrideMaterials is kept in lockstep with OriginalMaterials for symmetry
 * and debug inspection — it also pins the MID we created from the user's
 * OcclusionMaterial, so that reloading / hot-reloading the source material
 * doesn't immediately invalidate our swap.
 */
USTRUCT()
struct FComposableCameraOcclusionMaterialOverride
{
	GENERATED_BODY()

	/** The primitive whose material slots we swapped. Weak ref so actor
	 *  destruction (e.g. NPC pooled away) cleans itself up — we prune stale
	 *  entries each tick. */
	UPROPERTY()
	TWeakObjectPtr<UPrimitiveComponent> Component;

	/** Original UMaterialInterface per element index, captured at swap time. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;

	/** MIDs we created from the occlusion material per element index.
	 *  Recorded so the node holds a hard GC reference to them for the
	 *  lifetime of the override. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;
};

/**
 * Fades objects between the camera and a target actor (or near the camera)
 * by replacing their materials with a user-supplied transparency material.
 *
 * Two independent detection paths feed the same material-swap pipeline:
 *
 *  A. **Line-of-sight occlusion** (bFadeOccluders). Async multi-sphere sweep
 *     from the camera to the target each frame; every primitive hit that
 *     passes the tag / mesh-type filters is fade-marked.
 *
 *  B. **Proximity fade** (bFadeNearbyActors). Sphere overlap at the camera
 *     position each frame; every actor of class ProximityActorClass within
 *     ProximityRadius has its fade-eligible components fade-marked.
 *
 * Both paths produce a union set of primitives to fade this frame. Delta
 * tracking against AppliedMaterialOverrides means we only call SetMaterial /
 * CreateDynamicMaterialInstance on primitives entering or leaving the set —
 * the steady state is zero per-frame material work.
 *
 * Fade shape and timing are entirely encoded in OcclusionMaterial's shader
 * (dither, fresnel, Time-driven opacity — your call). The node does instant
 * material swaps; any smooth cross-fade lives in the shader. This follows
 * Epic's UOcclusionMaterialCameraNode design: no material contract beyond
 * "point at the occlusion material asset".
 *
 * Lifecycle:
 *  - OnInitialize seeds state and restores any stale overrides.
 *  - OnTickNode runs both detection paths and applies the delta.
 *  - BeginDestroy restores every remaining override — mandatory to avoid
 *    leaving actors stuck in the transparency material after the camera is
 *    popped / destroyed.
 *
 * The sweep path uses the async trace API (submit frame N, consume frame
 * N+1). Occluder decisions lag by one frame, which is visually acceptable
 * and keeps the game thread off the physics query's critical path.
 *
 * StaticMeshComponent-only is NOT enforced — unlike Epic's node, we consider
 * any UPrimitiveComponent subclass (static mesh, skeletal mesh, instanced,
 * geometry collection) subject to bAffectStaticMeshes / bAffectSkeletalMeshes.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Fades occluding or proximate primitives by swapping their materials for a user-supplied transparency material."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraOcclusionFadeNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	virtual void BeginDestroy() override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// ─── Target (aligned with CollisionPushNode's pivot pattern) ──────────

	/** Actor whose location (plus PivotZOffset, or bone socket) is the
	 *  "protected end" of the line-of-sight sweep and the hit-test anchor
	 *  for proximity fade. Typically the player pawn. Must be non-null when
	 *  bFadeOccluders is true; otherwise the sweep is skipped with a warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<AActor> PivotActor { nullptr };

	/** World-Z offset added on top of the actor location when bUseBoneForDetection
	 *  is false (or the requested bone can't be found). Typically ~50 to raise
	 *  the target from foot to chest so the sweep line doesn't graze the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == false"))
	float PivotZOffset { 50.f };

	/** When true, the target point is the named bone's world location on the
	 *  actor's skeletal mesh (if present and the bone resolves). Falls back to
	 *  ActorLocation + PivotZOffset on any failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bUseBoneForDetection { false };

	/** Bone / socket name sampled on the actor's skeletal mesh when
	 *  bUseBoneForDetection is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == true"))
	FName BoneName;

	// ─── Shared fade mechanism ────────────────────────────────────────────

	/** Transparency material swapped onto every faded primitive's material
	 *  slots. The shader inside this material owns the fade look — dither,
	 *  fresnel, opacity — and any smooth fade-in/out animation. Must be set
	 *  or the node no-ops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<UMaterialInterface> OcclusionMaterial { nullptr };

	/** Whether static mesh components are eligible for fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bAffectStaticMeshes { true };

	/** Whether skeletal mesh components are eligible for fade. Enabled by
	 *  default — Epic's node misses this, but NPCs / characters blocking the
	 *  view are one of the most common fade cases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bAffectSkeletalMeshes { true };

	/** Extra actors to exclude from both the sweep and the proximity query.
	 *  PivotActor is ignored automatically — this list is for teammates,
	 *  companions, vehicles, or any other actor that must not fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TArray<TObjectPtr<AActor>> ExtraIgnoredActors;

	// ─── Scenario A: line-of-sight occlusion ──────────────────────────────

	/** Master switch for line-of-sight occlusion detection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Occlusion)
	bool bFadeOccluders { true };

	/** Collision channel used by the async sphere sweep from camera to target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Occlusion, meta = (EditCondition = "bFadeOccluders == true"))
	TEnumAsByte<ECollisionChannel> OcclusionChannel { ECC_Camera };

	/** Radius of the sweep sphere in world units. Widen for short/thin
	 *  occluders that a thin line trace would miss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Occlusion, meta = (EditCondition = "bFadeOccluders == true", ClampMin = "0.0"))
	float OcclusionSphereRadius { 10.f };

	/** Optional component-tag filter. When NAME_None (default), every primitive
	 *  the sweep hits is eligible (same behaviour as Epic's node, where
	 *  collision channel alone decides). When non-empty, only components
	 *  carrying this tag via UActorComponent::ComponentTags are considered —
	 *  the usual way to say "walls on ECC_Camera stay solid, tagged foliage
	 *  fades". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Occlusion, meta = (EditCondition = "bFadeOccluders == true"))
	FName OccluderComponentTag;

	// ─── Scenario B: proximity fade ───────────────────────────────────────

	/** Master switch for proximity-based fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Proximity)
	bool bFadeNearbyActors { true };

	/** Radius of the proximity overlap centred at the camera location. Actors
	 *  of class ProximityActorClass whose bounding shape intersects this
	 *  sphere are fade-marked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Proximity, meta = (EditCondition = "bFadeNearbyActors == true", ClampMin = "0.0"))
	float ProximityRadius { 100.f };

	/** Actor class filter for proximity fade. Null = treat as APawn. Use a
	 *  narrower class (e.g. a game-specific ACharacter subclass) to exclude
	 *  vehicles / AI turrets / etc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Proximity, meta = (EditCondition = "bFadeNearbyActors == true"))
	TSubclassOf<AActor> ProximityActorClass;

	/** When true, PivotActor is excluded from proximity fade even if it lies
	 *  within ProximityRadius. Defaults to false — the typical "fade the
	 *  player when camera vision gets too close" pattern wants PivotActor
	 *  included. Flip on for cameras where the player body must never
	 *  disappear (cinematic, over-shoulder with forced full-body view). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Proximity, meta = (EditCondition = "bFadeNearbyActors == true"))
	bool bIgnorePivotActorInProximity { false };

private:
	// ─── Per-activation state (reset in OnInitialize) ─────────────────────

	/** Persistent record of every primitive we've swapped a material on. The
	 *  struct pins the original materials via UPROPERTY TObjectPtr so GC
	 *  doesn't collect them while we hold them. Pruned each tick for stale
	 *  weak refs (actor destroyed) and for components leaving the faded set. */
	UPROPERTY(Transient)
	TArray<FComposableCameraOcclusionMaterialOverride> AppliedMaterialOverrides;

	/** Async trace handle for the sweep submitted last frame and consumed this
	 *  frame. Invalidated after each successful read. */
	FTraceHandle PendingSweepHandle;

	/** Cached target point resolved this tick — used both by the sweep submit
	 *  and by debug draw. */
	FVector LastResolvedTargetPoint { FVector::ZeroVector };

	/** Cached camera position resolved this tick — same reason. */
	FVector LastCameraPosition { FVector::ZeroVector };

	// ─── Helpers ──────────────────────────────────────────────────────────

	/** Resolve the target world location from PivotActor + BoneName / PivotZOffset.
	 *  Returns false when PivotActor is null. */
	bool ResolveTargetPoint(FVector& OutTargetPoint) const;

	/** Consume the result of a sweep submitted on a previous frame (if any)
	 *  and insert its hits into OutFadableComponents after filtering. */
	void ConsumePendingSweep(UWorld* World, TSet<UPrimitiveComponent*>& OutFadableComponents);

	/** Submit a fresh async sphere sweep from camera to target. Handle stored
	 *  in PendingSweepHandle for next frame's consume. */
	void SubmitOcclusionSweep(UWorld* World, const FVector& CameraPos, const FVector& TargetPos);

	/** Run the synchronous proximity overlap at the camera position, collect
	 *  all fadable components on matching actors into OutFadableComponents. */
	void RunProximityQuery(UWorld* World, const FVector& CameraPos, TSet<UPrimitiveComponent*>& OutFadableComponents) const;

	/** Whether this primitive passes the mesh-type + component-tag filters.
	 *  OccluderContext toggles the component-tag check (proximity fade does
	 *  not use it; only the sweep path does). */
	bool PassesFadeFilters(UPrimitiveComponent* Component, bool bApplyOccluderTagFilter) const;

	/** Gather every UPrimitiveComponent on Actor that passes the mesh-type
	 *  filter into Out. The component-tag filter is deliberately skipped —
	 *  it's sweep-only. */
	void CollectFadableComponentsOnActor(AActor* Actor, TSet<UPrimitiveComponent*>& Out) const;

	/** Apply the occlusion material to every slot on Component, recording
	 *  originals in AppliedMaterialOverrides. No-op if Component is already
	 *  recorded. */
	void ApplyOcclusionMaterial(UPrimitiveComponent* Component);

	/** Restore originals and remove the record at AppliedMaterialOverrides[Index].
	 *  Index must be valid; caller is responsible. The array is RemoveAtSwap'd
	 *  so Index becomes invalid after the call — iterate backwards when
	 *  removing many. */
	void RestoreAndRemoveOverrideAt(int32 Index);

	/** Restore every currently-tracked override. Called from BeginDestroy and
	 *  from OnInitialize (in case a re-activated node inherited stale state). */
	void RestoreAllOverrides();

#if !UE_BUILD_SHIPPING
	/** Frame-snapshot of the sweep endpoints and proximity sphere, written by
	 *  OnTickNode and read by DrawNodeDebug. */
	mutable FVector DebugSweepStart { FVector::ZeroVector };
	mutable FVector DebugSweepEnd   { FVector::ZeroVector };
	mutable bool    bDebugSweepSubmittedThisTick { false };
#endif
};
