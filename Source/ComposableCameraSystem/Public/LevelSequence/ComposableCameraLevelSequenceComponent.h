// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraShot.h"
#include "LevelSequence/ComposableCameraTypeAssetReference.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraLevelSequenceComponent.generated.h"

class AComposableCameraCameraBase;
class UCineCameraComponent;
class UComposableCameraTransitionDataAsset;
class UMovieSceneSequencePlayer;
class UMovieSceneComposableCameraPatchSection;
class UMovieSceneComposableCameraShotSection;
struct FComposableCameraPose;

/**
 * Per-section editor-preview overlay state owned by
 * UComposableCameraLevelSequenceComponent::SequencerPatchOverlays.
 *
 * Top-level USTRUCT (not nested) because UHT rejects USTRUCT-inside-UCLASS.
 * One entry per active patch section; map key is the section pointer.
 */
USTRUCT()
struct FComposableCameraSequencerPatchOverlay
{
	GENERATED_BODY()

	/** Lazy-spawned evaluator actor for this section. Created on first
	 *  SetSequencerPatchOverlay; destroyed on RemoveSequencerPatchOverlay
	 *  or when the owning component is unregistered. */
	UPROPERTY()
	TObjectPtr<AComposableCameraCameraBase> Evaluator;

	/** Latest parameter block sampled from the section's channels at the
	 *  current playhead frame; re-pushed every frame the section is in-range
	 *  so per-tick application sees animated values. */
	UPROPERTY()
	FComposableCameraParameterBlock LatestParameters;

	/** Envelope alpha at the current frame, computed by the caller via
	 *  PatchEnvelope::ComputeStatelessAlpha. 0 = no contribution, 1 = full. */
	float Alpha = 0.f;
};

/**
 * Per-section Shot override state owned by
 * UComposableCameraLevelSequenceComponent::SequencerShotOverrides (Phase E,
 * extended for Phase F inter-Shot transitions).
 *
 * Top-level USTRUCT (not nested) because UHT rejects USTRUCT-inside-UCLASS.
 * One entry per active Shot Section; map key is the section weak pointer.
 *
 * RowIndex + EnterTransition + BlendAlpha together let the LSComponent's
 * `ApplyActiveSequencerShotOverride` blender pick the lowest-two RowIndex
 * entries (Phase F) and produce a blended camera pose using the higher
 * entry's resolved transition and alpha.
 */
USTRUCT()
struct FComposableCameraSequencerShotEntry
{
	GENERATED_BODY()

	/** Latest Shot data sampled from the section at the current playhead.
	 *  Re-pushed every frame the section is in-range (cheap value copy -
	 *  Shot's TArray<FShotTarget> is short and the rest is POD). */
	UPROPERTY()
	FComposableCameraShot Shot;

	/** Section row index. Top-row (lowest index) was the V1 winner; Phase F
	 *  blender picks the lowest two and blends. */
	int32 RowIndex = 0;

	/**
	 * Resolved EnterTransition asset for this section, loaded synchronously
	 * by the TrackInstance from the section's `EnterTransition` soft-ref each
	 * frame. Null if the section's EnterTransition is unset or fails to load -
	 * the blender treats null as a hard cut (the incoming section's pose
	 * snaps in at the boundary; no transition pose-blend pass runs).
	 *
	 * UPROPERTY-tracked TObjectPtr so the loaded asset stays referenced for
	 * the lifetime of this entry (the soft-pointer load can otherwise be
	 * GC'd between frames if no other reference exists).
	 */
	UPROPERTY()
	TObjectPtr<UComposableCameraTransitionDataAsset> EnterTransition;

	/**
	 * Blend progress for this entry as the *incoming* (higher-row) side of
	 * an overlap with the immediately-below RowIndex in-range peer. Range
	 * [0, 1].
	 *
	 * Computed each frame by the TrackInstance:
	 *   overlap_start = max(this.start, peer.start)   // intersection
	 *   overlap_end   = min(this.end,   peer.end)
	 *   BlendAlpha    = saturate( (CurrentFrame - overlap_start)
	 *                            / (overlap_end - overlap_start) )
	 *
	 * Defaults to 1.0 when the entry has no lower-row overlapping peer (the
	 * blender treats this as standalone, equivalent to V1's single-section
	 * write-through). The lower-row entry of an overlapping pair also keeps
	 * BlendAlpha = 1.0. Only the *higher-row* (incoming) entry's BlendAlpha
	 * is read by the blender; the lower-row entry's contribution is
	 * implicitly (1 - higher_entry.BlendAlpha) and computed inside the blender.
	 */
	float BlendAlpha = 1.0f;
};

/**
 * Actor component that drives a composable camera in the Level Sequence path.
 *
 * Holds a FComposableCameraTypeAssetReference (TypeAsset + editable Parameters
 * / Variables bags) and references an OutputCineCameraComponent on the same
 * Actor. On activation, the component spawns a transient
 * AComposableCameraCameraBase, runs its nodes each tick (without a
 * PlayerCameraManager), and projects the resulting pose onto the CineCamera
 * so Sequencer's Camera Cut Track and viewport Pilot both see the CCS camera
 * natively via the standard UCameraComponent path.
 *
 * Pure UActorComponent -NOT a USceneComponent. The component holds no
 * transform of its own; it is a logic-and-data driver. The owning Actor is
 * expected to provide a UCineCameraComponent as its RootComponent (that's
 * what AComposableCameraLevelSequenceActor does), and to hand us the
 * reference via OutputCineCameraComponent during construction. If a designer
 * adds this component to an arbitrary Actor (BlueprintSpawnableComponent),
 * OnRegister falls back to FindComponentByClass<UCineCameraComponent>(GetOwner())
 *. The component will then drive whatever CineCamera is first found on the
 * owning Actor, or be a no-op if none exists.
 *
 * Why ActorComponent not SceneComponent
 * -------------------------------------
 * Previously this was a USceneComponent whose RootComponent role doubled as
 * a parent for the CineCamera child. That arrangement collided with UE's
 * DefaultSubobject semantics (a component creating its own
 * CreateDefaultSubobject<UCineCameraComponent> registered the CineCamera as
 * a sub-subobject of the component, invisible to the Actor's component tree
 * and therefore invisible to USceneComponent::GetChildrenComponents and
 * AActor::FindComponentByClass<UCameraComponent>). PCM::SetViewTarget's
 * implicit-activation filter relies on that traversal and silently bailed,
 * which manifested as "second camera never activates" for blended Camera
 * Cut sections. See the diagnostic log that uncovered it.
 *
 * With the CineCamera as the Actor's RootComponent and this component as a
 * plain sibling UActorComponent, the engine's standard "find a CameraComponent
 * on the actor" path trivially finds the root, PCM::SetViewTarget creates
 * the proxy, and we go down the same fast path ACineCameraActor uses. No
 * special-case walks needed.
 *
 * Compatibility & responsibilities remain unchanged:
 *   - PCM-independent evaluation (via UE::ComposableCameras::ConstructCameraFromTypeAsset).
 *   - Per-tick bag ->RuntimeDataBlock re-sync before TickCamera.
 *   - Pose projection to OutputCineCameraComponent (position + rotation only).
 *   - On-demand tick gating via SetEvaluationEnabled.
 */
UCLASS(ClassGroup = (ComposableCameraSystem), meta = (BlueprintSpawnableComponent))
class COMPOSABLECAMERASYSTEM_API UComposableCameraLevelSequenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UComposableCameraLevelSequenceComponent();

	/** The TypeAsset reference + its per-instance parameter and variable bags.
	 *  Editing TypeAsset from the Details panel rebuilds the bags on
	 *  PostEditChangeProperty; editing individual parameter values rebuilds
	 *  the internal camera so the new values are reflected in the pose.
	 *  Not BlueprintReadWrite: the nested FInstancedPropertyBag fields are not
	 *  Blueprint-supported (see the struct comment). */
	UPROPERTY(EditAnywhere, Category = "Composable Camera")
	FComposableCameraTypeAssetReference TypeAssetReference;

	/** Reference to the Actor's UCineCameraComponent used as the viewport
	 *  terminal. Assigned by the owning Actor's constructor (primary path)
	 *  or resolved in OnRegister via FindComponentByClass (fallback for
	 *  arbitrary Actor hosts). The component does not own lifetime of the
	 *  CineCamera. The Actor does.
	 *
	 *  No edit/visible specifier on purpose: `AComposableCameraLevelSequenceActor`
	 *  exposes the same `UCineCameraComponent` via its own `OutputCineCameraComponent`
	 *  UPROPERTY (the surface designers actually use to author optics). Adding
	 *  Visible/EditAnywhere here would create TWO UPROPERTY paths reaching the
	 *  same component instance. When the Details panel walks the actor's
	 *  property map, `UpdateSinglePropertyMapRecursive` follows both paths,
	 *  hits the component on the second path, and recurses without cycle
	 *  detection (StackOverflow inside SDetailsView::SetObjects on Track / actor
	 *  selection). Plain `UPROPERTY()` keeps GC tracking + retains the value
	 *  through serialization but skips Details panel walking. */
	UPROPERTY()
	TObjectPtr<UCineCameraComponent> OutputCineCameraComponent;

	// UActorComponent.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// --- Sequencer-facing API --------------------------------------------

	/**
	 * Enable or disable component-driven evaluation.
	 *
	 * Level Sequence Spawn Tracks own actor lifetime. This flag is a local
	 * component switch used by teardown / external hosts; disabling destroys the
	 * transient internal camera, and enabling evaluates lazily on the next tick.
	 */
	void SetEvaluationEnabled(bool bEnabled);
	bool IsEvaluationEnabled() const { return bEvaluationEnabled; }

	/** Access the internal camera for editor-side inspection / debugging. */
	AComposableCameraCameraBase* GetInternalCamera() const { return InternalCamera; }

	/** External "TypeAsset was just swapped" entry for non-Details-panel paths
	 *  (e.g. the Sequencer track editor's "Camera Type Asset" picker). Performs
	 *  the same chain `PostEditChangeProperty` does on a TypeAsset edit:
	 *  rebuild parameter / variable bag layouts from the new asset, then
	 *  destroy + respawn the InternalCamera so the next tick reflects the
	 *  new TypeAsset. Caller is responsible for setting `TypeAssetReference.TypeAsset`
	 *  before calling this. Marks the component dirty for transaction tracking. */
	void NotifyTypeAssetExternallyChanged();

	// --- Sequencer Patch Overlay (driven by Patch TrackInstance) ---------
	//
	// Per-frame the Patch track's TrackInstance::OnAnimate walks its in-range
	// sections, resolves each section's TargetActorBinding to its bound LS
	// Actor, and pushes (section, sampled parameter block, envelope alpha)
	// here. This LS Component's own TickComponent walks the registered overlays
	// after ticking InternalCamera, runs each overlay's cached evaluator with
	// the running pose as input, and blends the result by alpha. Final patched
	// pose (Position + Rotation + FOV) is projected onto the CineCamera.
	//
	// Same path runs in BOTH editor preview (Sequencer scrub in the editor
	// viewport) AND PIE (Camera Cut Track targets the LS Actor). Spawn Tracks
	// own LS Actor lifetime; patches naturally apply only while their host
	// component exists and ticks.
	//
	// Patches added via the BP library `AddCameraPatch(PlayerIndex, ContextName, ...)`
	// are a separate path that lives on the gameplay PCM/Director, not here.
	// The two surfaces are intentionally orthogonal.
	//
	// 1-frame lag note: TrackInstance::OnAnimate may run AFTER this component's
	// TickComponent within a given frame, in which case the overlay state used
	// for projection is the previous frame's. Visually imperceptible during
	// scrub / playback; intentional trade-off vs. a more invasive ordering hack.

	/** Push (or refresh) an overlay registration for `Section`. Called every
	 *  frame the section is in-range. The pre-computed `EnvelopeAlpha` (from
	 *  the section's playhead position via `PatchEnvelope::ComputeStatelessAlpha`)
	 *  drives the BlendBy. The component caches a transient evaluator actor
	 *  per section (lazy-spawned on first use, destroyed on Remove or component
	 *  teardown). Intentionally accepts the parameter block by value. Caller
	 *  builds it per-frame from the section's channel curves. */
	void SetSequencerPatchOverlay(
		UMovieSceneComposableCameraPatchSection* Section,
		const FComposableCameraParameterBlock& Parameters,
		float EnvelopeAlpha);

	/** Remove an overlay registration when the section leaves its range or
	 *  when the TrackInstance shuts down. Destroys the cached evaluator actor.
	 *  Idempotent. Safe to call on a section that wasn't registered. */
	void RemoveSequencerPatchOverlay(UMovieSceneComposableCameraPatchSection* Section);

	/** Capture this LS Component's currently-registered Sequencer patch overlays
	 *  as Debug Panel snapshot rows. Called by the panel's `BuildPatchesLines`
	 *  (it walks every LS Component in the world and merges results with the
	 *  PatchManager-side snapshot). One snapshot row per overlay, sorted by
	 *  the section's resolved LayerIndex (matches the per-tick apply order so
	 *  the panel rows reflect actual composition order). Each entry has
	 *  `Source = EComposableCameraPatchSource::Sequencer` and `HostActorName`
	 *  populated so the renderer can prefix "[Seq]" / suffix "on Actor". */
	void BuildSequencerPatchSnapshot(TArray<struct FComposableCameraPatchSnapshot>& OutPatches) const;

	// --- Sequencer Shot Override (driven by Shot TrackInstance, Phase E) -----
	//
	// Per-frame the Shot track's TrackInstance::OnAnimate walks its in-range
	// sections, resolves the parent-binding's bound LS Actor, and pushes
	// (Section, Shot, RowIndex) here. This LS Component's TickComponent
	// applies the active override BEFORE InternalCamera->TickCamera, by
	// writing the Shot into the first found UComposableCameraCompositionFramingNode
	// inside the InternalCamera's CameraNodes array.
	//
	// Multi-section semantics (V1): top-row winner. The override entry with
	// the lowest RowIndex is applied; sections on lower rows are silently
	// skipped. Phase F replaces this picker with a multi-Shot blender for
	// transition-zone overlap.
	//
	// Section exit: CompositionFramingNode::Shot retains the last-written
	// value when no override is active (gap between sections / past the
	// final section). Camera holds last framing.
	//
	// 1-frame lag: TrackInstance::OnAnimate may run AFTER this component's
	// TickComponent within a given frame, in which case the override applied
	// is the previous frame's. Visually imperceptible during scrub /
	// playback; see DesignDoc / handoff for the explicit accept-1-frame-lag
	// design decision.

	/** Push (or refresh) a Shot override for `Section`. Called every frame
	 *  the section is in-range. The `InEntry` carries the resolved Shot,
	 *  RowIndex, EnterTransition, and pre-computed BlendAlpha. The
	 *  TrackInstance does the cross-section overlap analysis once per frame
	 *  and the LSComponent blender just consumes the result. */
	void SetSequencerShotOverride(
		UMovieSceneComposableCameraShotSection* Section,
		const FComposableCameraSequencerShotEntry& InEntry);

	/** Remove a Shot override when the section leaves its range or the
	 *  TrackInstance shuts down. Idempotent. */
	void RemoveSequencerShotOverride(UMovieSceneComposableCameraShotSection* Section);

private:
	/** Transient internal camera. Spawned lazily on first evaluation. Not
	 *  added to any context stack or director; driven entirely by this
	 *  component's TickComponent. */
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraCameraBase> InternalCamera;

	/** Local evaluation switch; see SetEvaluationEnabled. */
	bool bEvaluationEnabled = false;

	/** One-shot zero-delta evaluation consumed by TickComponent after enabling. */
	bool bEvaluateNextTickWithZeroDelta = false;

	/** Spawn InternalCamera if it doesn't exist yet, then call
	 *  ConstructCameraFromTypeAsset with the current bag values. Safe to call
	 *  repeatedly; reuses an existing camera when the TypeAsset hasn't changed. */
	void EnsureInternalCamera();

	/** Destroy InternalCamera and spawn a fresh one. Called from
	 *  PostEditChangeProperty when TypeAsset changes. */
	void RebuildInternalCamera();

	/** Project a pose into OutputCineCameraComponent. Position and rotation
	 *  are the only fields written; physical optics stay on the CineCamera
	 *  (designer or Sequencer property tracks drive them). */
	void ProjectPoseToCineCamera(const FComposableCameraPose& Pose);

	/** Destroy the internal camera actor if one exists. */
	void DestroyInternalCamera();

	/** Apply every active editor-preview patch overlay (sorted by the section's
	 *  resolved LayerIndex) onto `InOutPose`. Called from TickComponent in
	 *  editor world only, between InternalCamera->TickCamera and
	 *  ProjectPoseToCineCamera. Lazy-spawns evaluator actors as needed and
	 *  prunes stale entries (section GC'd) from the overlay map. */
	void ApplySequencerPatchOverlays(FComposableCameraPose& InOutPose, float DeltaTime);

	/** Resolve a DeltaTime that follows the owning Level Sequence playback
	 *  speed. Falls back to world DeltaTime when this component is not owned by
	 *  a Sequencer-spawned actor or no player can be found. */
	float ResolveSequencerAwareDeltaTime(float WorldDeltaTime);

	/** Find the runtime sequence player whose spawn register owns this
	 *  component's Actor. Editor preview resolves through EditorHooks instead. */
	UMovieSceneSequencePlayer* ResolveOwningSequencePlayer();

	/** Active overlays keyed by section. Key is `TWeakObjectPtr` (NOT
	 *  `TObjectPtr`) so a stale section that's been GC'd can actually go
	 *  stale. A strong-ref key would keep every Sequencer-side patch
	 *  section alive forever, defeating the prune-on-tick path in
	 *  `ApplySequencerPatchOverlays` that exists precisely to clean up
	 *  overlays whose source section has been destroyed (Sequencer rebuild,
	 *  asset reimport, undo across the section creation, etc.). TMap with
	 *  TWeakObjectPtr keys cannot be UPROPERTY-reflected, so the inner
	 *  `Evaluator` / `LatestParameters` UObject references are walked
	 *  manually in `AddReferencedObjects` below. Without that override,
	 *  the inner Evaluator actor would be GC-blind. The section pointer
	 *  itself stays alive via Sequencer's own TrackInstance / SectionInterface
	 *  ownership while it's a live edit target. */
	TMap<TWeakObjectPtr<UMovieSceneComposableCameraPatchSection>, FComposableCameraSequencerPatchOverlay> SequencerPatchOverlays;

	/** Runtime player cache used for Sequencer-aware DeltaTime scaling. Weak
	 *  because players can disappear during Sequencer rebuild / PIE teardown. */
	TWeakObjectPtr<UMovieSceneSequencePlayer> CachedOwningSequencePlayer;

public:
	/** Walk UObject references inside the non-UPROPERTY-reflected
	 *  `SequencerPatchOverlays` map. The map keys are weak (intentional -
	 *  see field doc above), so the TMap itself can't be UPROPERTY-tagged;
	 *  this override surfaces each overlay's `Evaluator` actor and the
	 *  UObject contents of its `LatestParameters` parameter block to GC.
	 *  (Same override also walks `SequencerShotOverrides`. See
	 *  `LSComponent.cpp::AddReferencedObjects` for the implementation.) */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	// --- Shot override storage --------------------------------------------

	/**
	 * Pick the top-row override (lowest RowIndex) and write its Shot into the
	 * first found `UComposableCameraCompositionFramingNode` on the
	 * InternalCamera's CameraNodes array. No-op when the map is empty (gap
	 * between sections -CompositionFramingNode keeps last-written Shot).
	 *
	 * Called from TickComponent BEFORE `InternalCamera->TickCamera` so the
	 * solver evaluates with the new Shot data on the same frame.
	 */
	void ApplyActiveSequencerShotOverride();

	/**
	 * Run one full evaluation pass (parameter block sync ->Shot override
	 * apply ->InternalCamera TickCamera ->patch overlays->CineCamera
	 * projection). Identical to a `TickComponent` body sans the
	 * `Super::TickComponent` / evaluation guard. Used by TickComponent and by
	 * the Shot override first-entry path, where Sequencer has already pushed
	 * the current Section data and the CineCamera must be re-projected before
	 * the cut renders.
	 *
	 * `DeltaTime <= 0` is the standard "first-frame snap" signal -
	 * downstream solvers (V2.2 IIR damping, scrub-aware nodes) treat it
	 * as "use authored values, don't damp", which matches the cut-as-cut
	 * design intent.
	 */
	void EvaluateOnce(float DeltaTime);

	/** Active Shot overrides keyed by Section. Held by `TWeakObjectPtr` to
	 *  tolerate GC of the section between frames (a common case during
	 *  Sequencer hot-reload / asset reimport).
	 *
	 *  **Not UPROPERTY**. Same constraint as `SequencerPatchOverlays`
	 *  above: TMap with `TWeakObjectPtr` keys cannot be UHT-reflected. The
	 *  inner `FComposableCameraSequencerShotEntry`'s UObject references
	 *  (`EnterTransition` TObjectPtr; `Shot` containing FShotTarget
	 *  TSoftObjectPtr / TObjectPtr resolved to actors) are walked manually
	 *  in `AddReferencedObjects` via `AddPropertyReferencesWithStructARO`
	 *  per entry, so reflection's blindness to the outer TMap doesn't
	 *  leave the resolved transition / target actors GC-blind. */
	TMap<TWeakObjectPtr<UMovieSceneComposableCameraShotSection>, FComposableCameraSequencerShotEntry> SequencerShotOverrides;

	/** Last frame's resolved *primary* Section (lowest RowIndex among the
	 *  active overrides). `ApplyActiveSequencerShotOverride` compares this
	 *  to the current frame's primary; mismatch = section transition.
	 *  The framing node either reseeds its primary prior state for a true
	 *  hard cut, or promotes the previous secondary prior when the incoming
	 *  Section becomes the new primary after an authored overlap. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UMovieSceneComposableCameraShotSection> LastActivePrimarySection;

	/** Last frame's resolved *secondary* Section (next-lowest RowIndex).
	 *  Used to recognize the normal overlap exit A+B->B, where B should
	 *  inherit the secondary prior cache instead of hard-seeding as a fresh
	 *  primary on the first post-blend frame. Also detects secondary swaps
	 *  like A+B->A+C so C starts from its own authored pose. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UMovieSceneComposableCameraShotSection> LastActiveSecondarySection;
};
