// Copyright Sulley. All rights reserved.

#include "DataAssets/ComposableCameraTargetInfo.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/**
	 * In-editor PIE remap: when the soft-ref resolves to an editor-world
	 * actor but a PIE session is running, walk world contexts to find a
	 * matching PIE-instance actor by FName + class. UE doesn't auto-fixup
	 * soft-pointer paths stored in UAssets when those paths target Level
	 * actors (only asset references are auto-fixed up by the cooker / PIE
	 * package-name patcher), so this is the standard manual workaround.
	 *
	 * Returns InActor unchanged when:
	 *   - Not running in editor (cooked / packaged builds — only one world
	 *     exists, so InActor is already the right one).
	 *   - InActor's world is already PIE / Game / EditorPreview (not the
	 *     persistent editor world that PIE was started from).
	 *   - No PIE counterpart with matching FName is found.
	 *
	 * Cost: O(world contexts) outer × O(1) StaticFindObject per inner. With
	 * typical contexts (Editor + PIE) this is ~2 hash lookups per call. The
	 * struct is read on the hot path of the Composition Solver but typical
	 * shots have ≤4 targets, so total per-frame cost is bounded.
	 */
	AActor* RemapToPIECounterpartIfNeeded(AActor* InActor)
	{
#if WITH_EDITOR
		if (!GIsEditor || !InActor || !GEngine)
		{
			return InActor;
		}
		const UWorld* InWorld = InActor->GetWorld();
		if (!InWorld || InWorld->WorldType != EWorldType::Editor)
		{
			return InActor;
		}

		// We're in editor build with InActor in the persistent editor world;
		// look for a PIE world that has a same-named actor (UE preserves
		// FName when duplicating the level into PIE).
		const FName InActorName = InActor->GetFName();
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType != EWorldType::PIE)
			{
				continue;
			}
			UWorld* PIEWorld = Ctx.World();
			if (!PIEWorld || !PIEWorld->PersistentLevel)
			{
				continue;
			}
			if (AActor* PIEActor = Cast<AActor>(StaticFindObject(
				AActor::StaticClass(), PIEWorld->PersistentLevel, *InActorName.ToString())))
			{
				return PIEActor;
			}
		}
#endif
		return InActor;
	}
}

bool FComposableCameraTargetInfo::ResolveWorldPoint(FVector& OutPoint, bool* OutUsedBone) const
{
	if (OutUsedBone)
	{
		*OutUsedBone = false;
	}

	// Soft-pointer Get() returns the loaded actor or nullptr — no
	// force-load. If the level holding the referenced actor isn't loaded,
	// resolution silently fails (caller can pre-seed OutPoint).
	AActor* ResolvedActor = Actor.Get();
	if (!ResolvedActor)
	{
		return false;
	}

	// PIE remap: if Actor.Get() returned the editor-world copy (because the
	// soft path was captured at asset-authoring time when only the editor
	// world existed), redirect to the PIE-world counterpart so live actor
	// positions reach the solver / debug gizmos. No-op in cooked builds and
	// in non-PIE editor states. See RemapToPIECounterpartIfNeeded above for
	// the rationale.
	ResolvedActor = RemapToPIECounterpartIfNeeded(ResolvedActor);

	FVector PivotBase;
	FQuat   PivotFrameRot;

	if (bUseBoneAsPivot && !BoneName.IsNone())
	{
		// Walk Actor's SkeletalMeshComponents looking for the bone /
		// socket. Multiple skeletal meshes are rare but possible; take
		// the first match so callers can control selection via component
		// order. Same pattern the inline FocusPullNode /
		// OcclusionFadeNode implementations used before consolidation.
		TArray<USkeletalMeshComponent*> SkelComps;
		ResolvedActor->GetComponents<USkeletalMeshComponent>(SkelComps);
		for (USkeletalMeshComponent* Skel : SkelComps)
		{
			if (Skel && Skel->DoesSocketExist(BoneName))
			{
				// One transform query (one socket-by-name lookup) instead of
				// two separate GetSocketLocation + GetSocketQuaternion calls.
				const FTransform SocketXform = Skel->GetSocketTransform(BoneName, RTS_World);
				PivotBase = SocketXform.GetLocation();
				PivotFrameRot = SocketXform.GetRotation();

				const FVector ResolvedOffset = bOffsetInLocalSpace
					? PivotFrameRot.RotateVector(Offset)
					: Offset;

				OutPoint = PivotBase + ResolvedOffset;

				if (OutUsedBone)
				{
					*OutUsedBone = true;
				}
				return true;
			}
		}
		// Bone not resolvable — fall through to the actor path. OutUsedBone
		// stays false so callers can apply legacy fallback offsets.
	}

	PivotBase = ResolvedActor->GetActorLocation();
	PivotFrameRot = ResolvedActor->GetActorQuat();

	const FVector ResolvedOffset = bOffsetInLocalSpace
		? PivotFrameRot.RotateVector(Offset)
		: Offset;

	OutPoint = PivotBase + ResolvedOffset;
	return true;
}

bool FComposableCameraTargetInfo::ResolveBasisQuat(FQuat& OutQuat) const
{
	AActor* ResolvedActor = Actor.Get();
	if (!ResolvedActor)
	{
		return false;
	}
	// Same PIE-remap path as ResolveWorldPoint — without this, basis would
	// come from the editor-world actor's authored quat instead of the live
	// PIE instance, identical bug to the pre-remap pivot resolution path.
	ResolvedActor = RemapToPIECounterpartIfNeeded(ResolvedActor);

	if (bUseSkeletalMeshForwardAsBasis)
	{
		// First SkelMeshComponent wins — same component-selection convention
		// as the bone path in ResolveWorldPoint. For ACharacter this is
		// always the `Mesh` property (the conventional main mesh) because
		// it's added first; multi-mesh actors with a non-default ordering
		// can use bUseBoneAsPivot + BoneName for finer control on the pivot
		// side, but for basis we just take the first mesh.
		if (USkeletalMeshComponent* SkelMesh =
				ResolvedActor->FindComponentByClass<USkeletalMeshComponent>())
		{
			OutQuat = SkelMesh->GetComponentQuat();
			return true;
		}
		// Silent fallback to actor quat — `bUseSkeletalMeshForwardAsBasis`
		// asks for the visual-mesh quat; when no SkelMesh exists, actor
		// quat is the natural degraded answer (mathematically equivalent
		// to authoring the flag off). This is intentional graceful
		// degradation, not a designer error: any non-Character actor
		// without a SkelMesh trivially hits this path. Warning here would
		// be noise. Designer-facing warning lives instead in
		// `ResolvePlacementBasis` for the Actor-null case (the path that
		// actually defeats the InheritFromActor intent — World identity
		// instead of any actor-derived quat).
	}

	OutQuat = ResolvedActor->GetActorQuat();
	return true;
}
