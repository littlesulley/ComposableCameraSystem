// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSection.h"
#include "UObject/SoftObjectPtr.h"
#include "MovieSceneComposableCameraShotSection.generated.h"

class UComposableCameraShotAsset;
class UComposableCameraTransitionDataAsset;

namespace UE::MovieScene { struct FSequenceInstance; }

/**
 * Per-target Actor override for a `UMovieSceneComposableCameraShotSection`.
 *
 * The framing data carried by a Shot (`FComposableCameraShot::Targets[i].Target.Actor`)
 * is a `TSoftObjectPtr<AActor>` which can only refer to actors that exist as
 * persistent / package-scoped instances in some level. That works for
 * Possessables in the level where the ShotAsset / Inline shot was authored,
 * but it breaks for:
 *   - Sequencer Spawnables (instantiated only while the section is alive),
 *   - Possessables in a different level than where the ShotAsset was authored,
 *   - reusable ShotAssets dragged across many sequences.
 *
 * Each override entry on the Section binds a TargetIndex inside the resolved
 * Shot's Targets array to a Sequencer FMovieSceneObjectBindingID. At
 * evaluation time, the TrackInstance resolves the binding through the running
 * sequence instance and substitutes the resulting actor into a value-copy of
 * the Shot — the underlying ShotAsset / InlineShot data is never mutated, so
 * the same ShotAsset can be reused across sections / sequences each with
 * their own bindings.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraShotTargetActorOverride
{
	GENERATED_BODY()

	/** Index into the resolved Shot's Targets array. Overrides for indices
	 *  outside the array (stale ShotAsset edit, mismatched count) are
	 *  silently dropped at evaluation time — designer's data isn't damaged
	 *  by a count-drift between authoring and runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override",
		meta = (ClampMin = "0"))
	int32 TargetIndex = 0;

	/** Sequencer binding whose resolved Actor replaces `Targets[TargetIndex].Target.Actor`.
	 *  Works with Spawnables, Possessables, and cross-sequence sub-bindings —
	 *  same picker UX as Camera Cut Track's CameraBindingID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Override")
	FMovieSceneObjectBindingID Binding;
};

/**
 * Source-of-truth for a Shot Section's framing data — Inline value-typed
 * struct or AssetReference soft-pointer. See `UComposableCameraShotAsset` and
 * spec §3.4.1.
 */
UENUM(BlueprintType)
enum class EComposableCameraShotSource : uint8
{
	/** `InlineShot` carries the Shot data directly inside the Section. One-off
	 *  framing for a specific moment. Good for shots that aren't reused
	 *  elsewhere and don't justify a separate asset. */
	Inline,

	/** `ShotAssetRef` soft-refs a `UComposableCameraShotAsset`. Editing the
	 *  asset propagates to every Section referencing it. Good for reusable
	 *  framing presets ("close-up A", "two-shot wide"). */
	AssetReference
};

/**
 * One section on a `UMovieSceneComposableCameraShotTrack` — represents a
 * single Shot activation window in the timeline.
 *
 * The section IS the addressing artifact:
 *   - WHEN the shot is active            → the section's TrueRange.
 *   - WHO it applies to                  → the bound `AComposableCameraLevelSequenceActor`
 *                                          resolved through the parent binding row
 *                                          (no per-section `TargetActorBinding` —
 *                                          unlike the Patch section which is root-level).
 *   - WHAT shot data it carries          → Inline `FComposableCameraShot` value
 *                                          OR soft-ref to a `UComposableCameraShotAsset`.
 *
 * Per-frame the `UMovieSceneComposableCameraShotTrackInstance::OnAnimate`:
 *   1. Resolves the parent binding → bound LS Actor → its
 *      `UComposableCameraLevelSequenceComponent`.
 *   2. Calls `ResolveActiveShot()` to get the active Shot data (Inline or
 *      AssetReference deref).
 *   3. Pushes (Section, Shot, RowIndex) to the LS Component via
 *      `SetSequencerShotOverride`.
 *
 * The LS Component's `TickComponent` then picks the top-row override
 * (`MinByRowIndex`) and writes its Shot into the first found
 * `UComposableCameraCompositionFramingNode::Shot` UPROPERTY on the internal
 * camera before `TickCamera` runs — so the framing solver evaluates with
 * the new data on the same frame.
 *
 * Phase F (inter-Shot transitions) will replace the top-row picker with a
 * blender; the multi-entry override map already supports this.
 *
 * No `UMovieSceneParameterSection` inheritance (unlike the Patch section) —
 * Shot fields are not designed for per-frame channel keying. Designers who
 * want a moving target should instead drive the underlying `Targets[i].Actor`
 * via Sequencer's standard transform tracks; the framing solver re-evaluates
 * each frame.
 */
UCLASS(MinimalAPI)
class UMovieSceneComposableCameraShotSection : public UMovieSceneSection
	, public IMovieSceneEntityProvider
{
	GENERATED_BODY()

public:
	UMovieSceneComposableCameraShotSection(const FObjectInitializer& ObjectInitializer);

	// IMovieSceneEntityProvider — emits a per-section TrackInstance dispatch
	// so the Shot track instance receives this section in its inputs every
	// in-range frame. Same shape as `UMovieSceneComposableCameraPatchSection`.
	virtual void ImportEntityImpl(
		UMovieSceneEntitySystemLinker* EntityLinker,
		const UE::MovieScene::FEntityImportParams& ImportParams,
		UE::MovieScene::FImportedEntity* OutImportedEntity) override;

	/**
	 * Resolves the active Shot for this section.
	 *
	 *   Inline          → returns &InlineShot.
	 *   AssetReference  → loads `ShotAssetRef` (synchronous; soft-pointer
	 *                     `LoadSynchronous`) and returns &Asset->Shot.
	 *                     Returns null if the asset is null or fails to load.
	 *
	 * Caller must NOT cache the returned pointer across frames — the
	 * AssetReference path may reload the asset, and the soft-ref target may
	 * be GC'd between calls. Treat as a per-frame snapshot.
	 *
	 * Const overload returns a const pointer for read-only callers (the Shot
	 * Editor's Sequencer-selection-sync uses this); non-const overload allows
	 * authoring tools that mutate Shot fields directly (the Shot Editor
	 * opened in AssetReference mode hosts the mutation on the *asset*, not
	 * the Section, so the Section doesn't need to write through).
	 *
	 * COMPOSABLECAMERASYSTEM_API: needed because UCLASS(MinimalAPI) only
	 * exports the class type info, not member functions, and the editor
	 * module's Shot Editor + track editor link against these.
	 */
	COMPOSABLECAMERASYSTEM_API const FComposableCameraShot* ResolveActiveShot() const;
	COMPOSABLECAMERASYSTEM_API FComposableCameraShot* ResolveActiveShot();

	/** Resolves the host UObject for the Shot Editor when this Section is
	 *  selected. Inline → the Section itself; AssetReference → the resolved
	 *  ShotAsset (or null if unresolved — Shot Editor falls through to its
	 *  "no shot loaded" placeholder). */
	COMPOSABLECAMERASYSTEM_API UObject* ResolveShotEditorHost() const;

	/**
	 * Build the effective Shot for this section + the running sequence
	 * instance. Starts from `ResolveActiveShot()` (Inline / AssetReference),
	 * value-copies it into `OutShot`, then walks `TargetActorOverrides` and
	 * substitutes each indexed `Targets[i].Target.Actor` with the override
	 * binding's resolved actor.
	 *
	 * Returns false (OutShot left unchanged) when the source Shot is
	 * unresolvable (AssetReference asset null / unloaded). Returns true with
	 * a populated OutShot otherwise — overrides whose binding doesn't
	 * resolve OR whose TargetIndex is out of range are silently dropped, so
	 * a section with stale overrides still produces a valid Shot.
	 *
	 * The underlying ShotAsset / InlineShot data is never mutated. The
	 * returned working copy is what the TrackInstance pushes into the LS
	 * Component's per-frame override map.
	 */
	COMPOSABLECAMERASYSTEM_API bool BuildEffectiveShot(
		const UE::MovieScene::FSequenceInstance& Instance,
		FComposableCameraShot& OutShot) const;

public:
	/** Source mode picker — Inline (data in Section) vs. AssetReference (data
	 *  in a ShotAsset). Default Inline because the common authoring flow is
	 *  one-off shots, and elevating an Inline shot to a reusable asset is
	 *  trivial later (Right-click → "Save as Shot Asset"; deferred to a
	 *  later polish step). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	EComposableCameraShotSource Source = EComposableCameraShotSource::Inline;

	/** Used iff `Source == Inline`. Edited via the Shot Editor (single-click
	 *  on the Section auto-swaps the editor's context — Phase E.5) or
	 *  inline in the Details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shot",
		meta = (EditCondition = "Source == EComposableCameraShotSource::Inline",
		        EditConditionHides))
	FComposableCameraShot InlineShot;

	/** Used iff `Source == AssetReference`. Soft-ref so the section doesn't
	 *  force-load the asset at section construction time / level streaming —
	 *  resolution happens lazily inside `ResolveActiveShot`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot",
		meta = (EditCondition = "Source == EComposableCameraShotSource::AssetReference",
		        EditConditionHides))
	TSoftObjectPtr<UComposableCameraShotAsset> ShotAssetRef;

	/** Per-target Actor binding overrides. Each entry binds a TargetIndex
	 *  in the resolved Shot's Targets array to a Sequencer binding picker;
	 *  the TrackInstance resolves the binding to an Actor at evaluation
	 *  time and substitutes it into the working Shot copy.
	 *
	 *  Primary use case: an AssetReference Section whose ShotAsset's
	 *  Targets reference a generic / placeholder Actor (or a Spawnable that
	 *  doesn't survive level boundaries) — the override pins the actor
	 *  resolution to a binding inside this sequence. Also useful for Inline
	 *  Sections when the Inline Shot's Targets reference Spawnables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot|Target Overrides")
	TArray<FComposableCameraShotTargetActorOverride> TargetActorOverrides;

	/**
	 * Transition asset that drives the inter-Shot blend when the playhead
	 * enters this Section from a previous overlapping Section on the same
	 * Shot Track (Phase F).
	 *
	 *  - When two Sections overlap in time, the lower-row Section is the
	 *    *outgoing* shot and the higher-row Section is the *incoming* shot
	 *    (top-row by RowIndex). The incoming Section's `EnterTransition`
	 *    selects how the two solver outputs blend together.
	 *  - The overlap window itself defines the blend duration. The
	 *    Transition asset's `TransitionTime` is ignored — designers control
	 *    duration via section overlap on the timeline; the transition asset
	 *    contributes its ease curve / blend math only (handoff §F decision Q4).
	 *  - Null = hard cut. The incoming Section snaps in at the boundary;
	 *    no blend is performed. Equivalent V1 top-row-winner behavior with
	 *    no overlap region treated as a transition.
	 *  - On the *first* Section's left edge (no previous overlapping
	 *    Section) `EnterTransition` is ignored — there is nothing to blend
	 *    from (handoff §F decision Q2).
	 *
	 * Soft-ref so the section doesn't force-load the transition asset at
	 * level streaming time; resolution happens lazily inside the
	 * TrackInstance per-frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot|Transition")
	TSoftObjectPtr<UComposableCameraTransitionDataAsset> EnterTransition;
};
