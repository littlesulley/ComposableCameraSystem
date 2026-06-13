// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraTargetInfo.h"
#include "ComposableCameraShotTarget.generated.h"

/**
 * Selects how the bounding box around a shot target is determined. The
 * bounding box drives the FOV solve when FOVMode == SolvedFromBoundsFit.
 * See DesignDoc / TechDoc for the weight-scaled perceptual union box.
 */
UENUM(BlueprintType)
enum class EShotTargetBoundsShape : uint8
{
	/** No bounding box. Target does not contribute to FOV solve. Most common. */
	None,

	/** Author-supplied half-extent (ManualBoundsExtent). Always cheap. */
	ManualExtent,

	/**
	 * Snapshot of Actor->GetComponentsBoundingBox() per BoundsCachePolicy.
	 * Walks the actor's component hierarchy each refresh. Never per-frame
	 * unless BoundsCachePolicy == Live.
	 */
	AutoFromComponentBounds
};

/**
 * Cache refresh policy for AutoFromComponentBounds bounds shape. Only
 * meaningful when BoundsShape == AutoFromComponentBounds.
 */
UENUM(BlueprintType)
enum class EBoundsCachePolicy : uint8
{
	/**
	 * Cached once when the Shot becomes the active shot in LS; never refreshed
	 * for the lifetime of the Shot section. Cheapest; right for non-deforming
	 * scene actors. Default.
	 */
	StaticSnapshot,

	/**
	 * Re-cached every BoundsRefreshIntervalFrames frames. Right for slowly-
	 * deforming actors (vehicles with moving parts, characters whose pose
	 * changes slowly).
	 */
	Periodic,

	/**
	 * Re-cached every frame. Most accurate, most expensive. Use sparingly,
	 * only for highly animated characters whose BB matters frame-to-frame.
	 */
	Live
};

/**
 * Per-Actor data within a single FComposableCameraShot. Targets are PURELY
 * world-space objects: identity (Actor + Bone + Offset via the embedded
 * `FComposableCameraTargetInfo`) plus an optional bounding box used by the
 * FOV bounds-fit solve. Targets do NOT carry screen-space composition
 * data. Screen-space composition lives on the Shot's Placement / Aim layers:
 * `FShotPlacement::ScreenPosition` for where the placement anchor lands, and
 * `FShotAim::ScreenPosition` for where the aim anchor lands.
 *
 * Properties are BlueprintReadOnly per the Shot system's "no runtime BP API
 * for mutating Shot data" principle.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraShotTarget
{
	GENERATED_BODY()

	/** Identity + pivot resolution. Drives the world point this target
	 *  contributes to all solver passes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot Target")
	FComposableCameraTargetInfo Target;

	// --- Optional bounding box (drives FOV solve when SolvedFromBoundsFit) -

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds")
	EShotTargetBoundsShape BoundsShape = EShotTargetBoundsShape::None;

	/** Half-extent in world units; used iff BoundsShape == ManualExtent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds",
		meta = (EditCondition = "BoundsShape == EShotTargetBoundsShape::ManualExtent"))
	FVector ManualBoundsExtent = FVector(50.f);

	/** Cache refresh policy when BoundsShape == AutoFromComponentBounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds",
		meta = (EditCondition = "BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds"))
	EBoundsCachePolicy BoundsCachePolicy = EBoundsCachePolicy::StaticSnapshot;

	/** Refresh interval in frames when BoundsCachePolicy == Periodic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds",
		meta = (EditCondition = "BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds && BoundsCachePolicy == EBoundsCachePolicy::Periodic", ClampMin = "1"))
	int32 BoundsRefreshIntervalFrames = 30;

	/**
	 * Snapshot of Actor->GetComponentsBoundingBox().GetExtent(). Populated
	 * via RefreshAutoBoundsCache(). Never read directly by user code; go
	 * through GetEffectiveBoundsExtent() which dispatches on BoundsShape.
	 *
	 * Marked Transient: the cache is rebuilt at Shot activation and is
	 * never serialized.
	 */
	UPROPERTY(Transient)
	FVector CachedAutoBoundsExtent = FVector::ZeroVector;

	/**
	 * Cached weak ref to the actor's first SkeletalMesh / StaticMesh
	 * component used by `RefreshAutoBoundsCache`. Polish P.1. Without this,
	 * `Live`-policy refreshes call `Actor->FindComponentByClass<...>()` per
	 * tick per target (`O(actor.Components.Num())` linear walk), which
	 * accumulates measurably on character actors with 30+ components in a
	 * 4-target Shot at 60fps. With the cache, `Live` reduces to a weak-ptr
	 * validity check + a `Bounds.GetBox()` read; only invalidation (actor
	 * swap, component destroyed) re-runs `FindComponentByClass`.
	 *
	 * Stored as `TWeakObjectPtr<UPrimitiveComponent>` (the common base of
	 * SkelMesh + StaticMesh) so the same field handles either class. Mutable
	 * so the const-method `GetEffectiveBoundsExtent` could still do a
	 * read-side validity check if needed; current callers only mutate via
	 * `RefreshAutoBoundsCache` (already non-const). Transient so the cache
	 * doesn't persist across save/load. Actors must re-resolve on first
	 * tick after load.
	 */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<class UPrimitiveComponent> CachedBoundsMeshComponent;

	/**
	 * Importance weight of this target's bounds in the FOV bounds-fit
	 * solve. Drives the BlackEye-style "perceptual union box" sizing:
	 * high-weight targets dominate the resulting box, low-weight
	 * ones contribute proportionally less. 0 = target has bounds but
	 * doesn't drive FOV. Clamped [0, 1]. Only ratios matter in the
	 * perceptual-box math, the [0, 1] convention keeps Details-panel
	 * intent readable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds",
		meta = (EditCondition = "BoundsShape != EShotTargetBoundsShape::None", ClampMin = "0.0", ClampMax = "1.0"))
	float BoundsContributionWeight = 1.f;

	// --- Helpers ---------------------------------------------------------

	/**
	 * Refreshes CachedAutoBoundsExtent from Actor's component bounds.
	 * No-op when BoundsShape != AutoFromComponentBounds OR Target.Actor is
	 * null. Call from:
	 *   (a) Shot Editor on Actor pick / BoundsShape toggle;
	 *   (b) Runtime when the Shot becomes active in LS;
	 *   (c) Periodic / Live tick if the policy demands.
	 *
	 * Walks the actor's component hierarchy via GetComponentsBoundingBox
	 * (O(component count)). Never call from per-frame code unless the
	 * cache policy is Live.
	 */
	void RefreshAutoBoundsCache();

	/**
	 * Returns the effective bounds half-extent based on BoundsShape:
	 *   None -> FVector::ZeroVector (target ignored by FOV solve)
	 *   ManualExtent -> ManualBoundsExtent
	 *   AutoFromComponentBounds -> CachedAutoBoundsExtent (zero if cache
	 *                            cold. Degrades silently to "no bounds")
	 */
	FVector GetEffectiveBoundsExtent() const;
};
