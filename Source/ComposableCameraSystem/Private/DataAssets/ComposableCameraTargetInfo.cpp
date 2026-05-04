// Copyright Sulley. All rights reserved.

#include "DataAssets/ComposableCameraTargetInfo.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/**
	 * In-editor PIE remap fallback: when the soft-ref resolves to an editor-
	 * world actor but a PIE session is running, walk world contexts to find a
	 * matching PIE-instance actor by FName + class. UE does NOT auto-fixup
	 * soft-pointer paths stored in UAssets when those paths target Level
	 * actors (only asset references are auto-fixed up by the cooker / PIE
	 * package-name patcher), so the path-rewrite branch in
	 * `ResolvePIEAwareActor` covers PersistentLevel paths and this fallback
	 * scoops up cases the path rewrite can't reach (sublevels, World
	 * Partition runtime cells whose runtime outer differs from the saved
	 * path, Spawnable instances whose names don't match across worlds).
	 *
	 * Returns InActor unchanged when:
	 *   - Not running in editor (cooked / packaged builds — only one world
	 *     exists, so InActor is already the right one).
	 *   - InActor's world is already PIE / Game / EditorPreview (not the
	 *     persistent editor world that PIE was started from).
	 *   - No PIE counterpart with matching FName is found.
	 *
	 * Two-pass per PIE world: a fast `StaticFindObject` against the
	 * `PersistentLevel` outer first (matches the common case where the
	 * editor actor is package-scoped to the persistent level), then a
	 * `TActorIterator` walk if the fast lookup misses (covers sublevel /
	 * streamed actors whose Outer is the streamed level, not the persistent
	 * level). The iterator pass is bounded by total actor count per PIE world
	 * but only fires on the slow path; cooked / non-editor paths hit none of
	 * this and `RemapToPIECounterpartIfNeeded` is a no-op there.
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

		const FName InActorName = InActor->GetFName();
		UClass* InActorClass = InActor->GetClass();
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

			// Fast path — same-named actor outered to the PIE world's
			// PersistentLevel. Covers "drop a level actor into Inline Shot
			// target" + non-WP levels (UE preserves FName when duplicating
			// the level into PIE).
			if (AActor* PIEActor = Cast<AActor>(StaticFindObject(
				AActor::StaticClass(), PIEWorld->PersistentLevel, *InActorName.ToString())))
			{
				return PIEActor;
			}

			// Slow path — sublevel / streamed-level / World Partition runtime
			// cell actors aren't outered to the persistent level; iterate the
			// PIE world to find them. Match on FName + class to keep the same
			// identity contract the fast path uses. TActorIterator skips
			// pending-kill actors so we don't return zombies.
			for (TActorIterator<AActor> It(PIEWorld, InActorClass); It; ++It)
			{
				if (AActor* Candidate = *It)
				{
					if (Candidate->GetFName() == InActorName)
					{
						return Candidate;
					}
				}
			}
		}
#endif
		return InActor;
	}

	/**
	 * Resolves a `TSoftObjectPtr<AActor>` to a live `AActor*`, with PIE-aware
	 * remapping. Used by both `ResolveWorldPoint` and `ResolveBasisQuat` so
	 * the two paths share identical resolution semantics.
	 *
	 * Resolution order:
	 *   1. **Path-rewrite (PIE only)** — when a PIE session is running, copy
	 *      the soft path and run UE's `FSoftObjectPath::FixupForPIE`. That
	 *      function rewrites the package portion of the path with the PIE
	 *      instance prefix (`/Game/Map.Map:PersistentLevel.Actor`
	 *      → `/Game/UEDPIE_0_Map.UEDPIE_0_Map:PersistentLevel.Actor`)
	 *      whenever the path looks like a level subobject reference, then
	 *      `ResolveObject` lands directly on the PIE-duplicated actor. This
	 *      is the standard UE idiom for level-actor soft-refs stored in
	 *      UAssets and handles the PersistentLevel case without walking world
	 *      contexts.
	 *   2. **Direct soft-pointer Get** — covers cooked / non-PIE paths and
	 *      any path the rewrite couldn't fix (including raw `AActor*`
	 *      assignments that already point at a PIE actor, e.g. the
	 *      `BuildEffectiveShot` override path).
	 *   3. **Walk-PIE-worlds fallback** — `RemapToPIECounterpartIfNeeded` on
	 *      the resolved actor. Catches editor-world actor that the path
	 *      rewrite couldn't redirect (sublevels, WP cells, Spawnables whose
	 *      names match across worlds even though the path doesn't).
	 *
	 * Returns null only when every path produces no live actor — caller
	 * treats that as "anchor unresolvable" (see `ShotSolver` warning).
	 */
	AActor* ResolvePIEAwareActor(const TSoftObjectPtr<AActor>& Soft)
	{
#if WITH_EDITOR
		const int32 PIEInstanceID = UE::GetPlayInEditorID();
		if (GIsEditor && PIEInstanceID != INDEX_NONE && !Soft.IsNull())
		{
			FSoftObjectPath FixedPath = Soft.ToSoftObjectPath();
			if (FixedPath.FixupForPIE(PIEInstanceID))
			{
				if (AActor* PIEActor = Cast<AActor>(FixedPath.ResolveObject()))
				{
					return PIEActor;
				}
				// Path rewrite succeeded but the rewritten object isn't
				// loaded — fall through to direct Get + walk fallback. Don't
				// LoadSynchronous: hot-path resolve, and a missing PIE-
				// duplicated actor at this point is almost certainly a
				// streaming-level / Spawnable timing issue that the walk
				// fallback handles by name.
			}
		}
#endif
		AActor* Direct = Soft.Get();
		if (!Direct)
		{
			return nullptr;
		}
		return RemapToPIECounterpartIfNeeded(Direct);
	}
}

bool FComposableCameraTargetInfo::ResolveWorldPoint(FVector& OutPoint, bool* OutUsedBone) const
{
	if (OutUsedBone)
	{
		*OutUsedBone = false;
	}

	// PIE-aware resolve: tries (1) UE's path-rewrite `FixupForPIE` for
	// PersistentLevel actor refs stored in UAssets, (2) direct soft-ptr Get
	// for cooked / already-correct paths, (3) walk-PIE-worlds fallback for
	// sublevel / WP / Spawnable cases the path rewrite can't reach. Returns
	// null only when no live actor can be resolved on any path — caller
	// treats that as "anchor unresolvable" so callers can pre-seed OutPoint.
	AActor* ResolvedActor = ResolvePIEAwareActor(Actor);
	if (!ResolvedActor)
	{
		return false;
	}

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
	// Same PIE-aware resolve as ResolveWorldPoint — without this, basis would
	// come from the editor-world actor's authored quat instead of the live
	// PIE instance, identical bug to the pre-remap pivot resolution path.
	AActor* ResolvedActor = ResolvePIEAwareActor(Actor);
	if (!ResolvedActor)
	{
		return false;
	}

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
