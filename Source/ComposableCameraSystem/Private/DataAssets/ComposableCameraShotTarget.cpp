// Copyright 2026 Sulley. All Rights Reserved.

#include "DataAssets/ComposableCameraShotTarget.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

void FComposableCameraShotTarget::RefreshAutoBoundsCache()
{
	if (BoundsShape != EShotTargetBoundsShape::AutoFromComponentBounds)
	{
		return;
	}

	AActor* Actor = Target.Actor.Get();
	if (!Actor)
	{
		// Leave the cache at whatever it was; the next valid refresh will
		// repopulate. GetEffectiveBoundsExtent returns Zero in this state.
		return;
	}

	// Use the first mesh component's world bounds (Skeletal preferred, then
	// Static), NOT `AActor::GetComponentsBoundingBox(bNonColliding=true)`. The
	// full-actor variant unions in EVERY registered PrimitiveComponent -	// CameraBoom / SpringArm endpoints, FollowCamera frustum bounds, debug
	// arrow / billboard primitives, attached weapon collision sweepers, etc.
	// On a stock UE5 ThirdPersonCharacter the union extends ~15m beyond the
	// visible character because the FollowCamera primitive at the end of a
	// 300cm SpringArm carries its own frustum bounds. The FOV bounds-fit
	// solve treats the extent as the visible silhouette envelope, so the
	// inflated BB inflates the solved FOV (or trips the strict
	// `bAllOnScreen` 8-vertex check and silently drops the target). Mesh-
	// component bounds match what the camera actually sees.
	//
	// Polish P.1: cache the resolved component to avoid `FindComponentByClass`
	// per tick per target. `Live` policy + 4-target Shot at 60fps would
	// otherwise burn 240 component-list walks/sec on character actors with
	// 30+ components. Validity check on the weak ptr is O(1); only first
	// resolve and recovery from owner-actor swaps re-walk the component
	// list. Cache MUST be invalidated when Target.Actor changes. Covered
	// by the `Component->GetOwner() == Actor` consistency check below.
	UPrimitiveComponent* MeshComp = CachedBoundsMeshComponent.Get();
	if (!MeshComp || MeshComp->GetOwner() != Actor)
	{
		MeshComp = nullptr;
		if (USkeletalMeshComponent* SK = Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			MeshComp = SK;
		}
		else if (UStaticMeshComponent* SM = Actor->FindComponentByClass<UStaticMeshComponent>())
		{
			MeshComp = SM;
		}
		CachedBoundsMeshComponent = MeshComp;
	}

	FBox WorldBox(ForceInit);
	if (MeshComp)
	{
		WorldBox = MeshComp->Bounds.GetBox();
	}
	else
	{
		// Actor has no SkelMesh / StaticMesh component (rare. Debug-only
		// / proxy-spawning fallback). Falls back to the full-actor bounds
		// box; CachedBoundsMeshComponent stays null so subsequent ticks
		// re-attempt the resolve cheaply if the actor gains a mesh later.
		WorldBox = Actor->GetComponentsBoundingBox(/*bNonColliding=*/true);
	}

	if (WorldBox.IsValid)
	{
		CachedAutoBoundsExtent = WorldBox.GetExtent();
	}
	else
	{
		// Actor has no components contributing to bounds (extremely rare).
		// Zero out to avoid using stale data from a previous actor.
		CachedAutoBoundsExtent = FVector::ZeroVector;
	}
}

FVector FComposableCameraShotTarget::GetEffectiveBoundsExtent() const
{
	switch (BoundsShape)
	{
	case EShotTargetBoundsShape::None:
		return FVector::ZeroVector;

	case EShotTargetBoundsShape::ManualExtent:
		return ManualBoundsExtent;

	case EShotTargetBoundsShape::AutoFromComponentBounds:
		// Cold cache (CachedAutoBoundsExtent == ZeroVector) silently
		// degrades to "no bounds" rather than crashing. Spec Section 5.4
		// documents this; the FOV solve will skip targets with zero
		// extent so the failure mode is "this target doesn't push the
		// FOV", not "wrong FOV".
		return CachedAutoBoundsExtent;
	}

	// Defensive: unknown enum case (should be unreachable).
	return FVector::ZeroVector;
}
