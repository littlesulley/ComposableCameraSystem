// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h" // FComposableCameraShot - value member CachedEffectiveShot needs full type
#include "DataAssets/ComposableCameraShotTarget.h"
#include "EditorViewportClient.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SShotEditorViewport.h" // EShotEditorMode

namespace ComposableCameraSystem::ShotEditorWheelMath
{
	/**
	 * Wheel-Distance per-click zoom factor (multiplicative).
	 *
	 * Default step is 10% (factor 1.1) - same rate PIE / Persona / stock
	 * editor viewport wheel dolly use. Modifier keys scale the *step*
	 * ( `factor 1`) to match DCC convention, so a single rate doesn't
	 * feel coarse-or-tedious depending on geometry scale:
	 *
	 * - default: 10% step -> factor 1.10
	 * - Shift: 50% step -> factor 1.50 (5x default - fast traversal)
	 * - Ctrl: 2% step -> factor 1.02 (0.2x default - fine framing)
	 * - Shift+Ctrl: default - composing 5x and 0.2x gives 1x (default),
	 * refusing to disambiguate is safer than picking
	 * arbitrarily.
	 *
	 * Pure function - extracted from `TryAdjustDistanceFromMouseWheel` so
	 * the modifier-key step logic has unit-test coverage independent of
	 * Slate viewport state.
	 */
	inline float ComputeWheelZoomFactor(bool bShiftHeld, bool bCtrlHeld, float DefaultStep = 0.1f)
	{
		float Step = DefaultStep;
		if (bShiftHeld != bCtrlHeld)
		{
			Step = bShiftHeld ? DefaultStep * 5.f: DefaultStep * 0.2f;
		}
		return 1.f + Step;
	}
}
class FPreviewScene;
class AActor;
class FScopedTransaction;
class FPrimitiveDrawInterface;
class FSceneView;
class USkeletalMesh;

/**
 * FEditorViewportClient subclass for the Shot Editor's middle region.
 *
 * Renders the Shot Editor's `FPreviewScene` (a separate UWorld from the user's
 * level), spawns one proxy actor per `Shot.Targets[i]` (snapshot of source actor's
 * mesh: SK / SM / capsule fallback in priority order), and drives the viewport
 * camera every tick from the Composition Solver's output.
 *
 * Camera control split (Q-A in design discussion):
 * - Default: solver fully drives camera (location, rotation, FOV).
 * - User starts a mouse drag -> `TrackingStarted` flips `bUserOverridingCamera`,
 * solver writes are suspended until release.
 * - User releases -> `TrackingStopped` clears the flag, solver takes over again
 * on the next tick. So the camera "snaps back" to the solved pose immediately
 * after a camera-track gesture, matching the "see what the solver decides"
 * intent.
 *
 * Proxy lifecycle:
 * - Proxies are spawned in the editor's `FPreviewScene`'s world (NOT the user's
 * level). They are GC-rooted via the world (`FPreviewScene::PreviewWorld` is
 * itself rooted by the scene's `AddReferencedObjects`). FEditorViewportClient
 * base already implements `FGCObject` - if we ever cache extra `UObject*`s
 * outside the scene, override its `AddReferencedObjects` rather than
 * re-inheriting FGCObject (which would be ambiguous).
 * - The cached `TWeakObjectPtr` array tracks them; `IsValid` guard is the
 * liveness check.
 * - Per-frame `Tick` syncs proxy transforms from their source actors. If
 * `Shot.Targets.Num()` changes between ticks (designer added / removed a target
 * in the Details panel) we rebuild the proxy set - this is cheap (handful of
 * actors max) and avoids needing a property-change delegate.
 *
 * Lifetime gotchas (per research Q7):
 * - Destructor must clear `Viewport` to nullptr BEFORE the SWidget tears down,
 * or `FEditorViewportClient` outlives `FSceneViewport` and the next Draw
 * dereferences a freed pointer. The owning `SShotEditorViewport` does this in
 * its own destructor.
 * - First-frame guard: `Viewport` is null until after `SEditorViewport::Construct`
 * returns. `Tick` checks for this and bails out.
 */
class FComposableCameraShotEditorViewportClient: public FEditorViewportClient
{
public:
	FComposableCameraShotEditorViewportClient(FPreviewScene* InPreviewScene,
		const TSharedRef<class SEditorViewport>& InEditorViewportWidget);

	virtual ~FComposableCameraShotEditorViewportClient();

	// External API used by SShotEditorViewport 

	/** Bind a new active Shot. Triggers a proxy rebuild on the next tick.
	 * Both args may be null - clears proxies + leaves the camera at last
	 * user-driven pose. */
	void SetActiveShot(FComposableCameraShot* InShot, UObject* InHost);

	/** Diagnostic accessor. Returns nullptr while no Shot is bound. */
	FComposableCameraShot* GetActiveShot() const { return ActiveShot; }

	/** Viewport mode (Drag / Free / Lock - see EShotEditorMode in
	 * SShotEditorViewport.h for semantics). */
	void SetMode(EShotEditorMode InMode);
	EShotEditorMode GetMode() const { return CurrentMode; }

	EShotEditorReverseSolveStatus DiagnoseReverseSolveCurrentCamera() const;
	bool CanReverseSolveCurrentCamera() const;
	bool ReverseSolveCurrentCameraToShot();

	void ResetViewToShot();

	bool GetShowDiagnosticHud() const { return bShowDiagnosticHud; }
	void SetShowDiagnosticHud(bool bInShowDiagnosticHud);

	bool GetShowCompositionGuides() const { return bShowCompositionGuides; }
	void SetShowCompositionGuides(bool bInShowCompositionGuides);

	/** Drain all scene-bound resources (currently: proxy actors) immediately.
	 * Called by the owning `SShotEditorViewport` from its destructor BEFORE
	 * the `FPreviewScene` member is destroyed.
	 *
	 * Why this matters: the base `SEditorViewport` also holds a TSharedPtr
	 * to this client, so our destructor doesn't fire when our owning widget
	 * releases its ref - the base's ref survives into `~SEditorViewport`,
	 * which runs AFTER `SShotEditorViewport`'s member destruction has already
	 * torn down `PreviewScene`. By that time `Proxy->Destroy()` would touch
	 * a dead world. Calling this method explicitly drains proxies while the
	 * scene is still alive. Idempotent. */
	void ReleaseSceneResources();

	// FEditorViewportClient overrides 

	virtual void Tick(float DeltaSeconds) override;

	/** Per-frame HUD overlay - draws aspect ratio, viewport size, camera pose,
	 * FOV, and resolved anchor's projected screen coords in the upper-left
	 * corner of the viewport. Diagnostic aid for verifying that the solver's
	 * output matches the renderer's projection across viewport resizes - 
	 * watching the overlay live while dragging the splitter / window border
	 * is the fastest way to catch any aspect-mismatch regression. */
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

	/** 3D primitive overlay - wireframe boxes around each Target's effective
	 * bounds, color-coded by whether the box would actually contribute to
	 * the `SolvedFromBoundsFit` FOV solve. Selection is implicit via the
	 * per-target `BoundsShape` + `BoundsContributionWeight` fields:
	 *
	 * - Drawn (green): all 8 BB vertices project in front of the camera
	 * (target IS feeding the FOV solve).
	 * - Drawn (yellow): BB valid but at least one vertex projects behind
	 * the camera plane -> solver's strict `bAllOnScreen`
	 * check drops the target. Common at close framings;
	 * nudge the camera back or reduce the manual extent.
	 * - Not drawn: `BoundsShape == None`, cold cache, OR
	 * `BoundsContributionWeight <= 0`. The designer's
	 * "select which actors contribute" mechanism - only
	 * authored-in targets render a box.
	 *
	 * Skipped entirely when `Lens.FOVMode != SolvedFromBoundsFit` - bounds
	 * are unread by Manual mode and the wireframes would be visual noise. */
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;

	// D.4 Handle drag input hooks 
	//
	// LMB down on a handle -> start drag (FScopedTransaction + Host->Modify);
	// LMB up while dragging->commit (PostEditChangeProperty ValueSet + end
	// transaction). Mouse moves during drag write into the active handle's
	// screen-position field on the Shot (Interactive change type).
	//
	// In Free / Lock modes, handles are drawn but greyed out and hit-test
	// is skipped - LMB falls through to base class in Free and is consumed
	// in Lock. Only Drag mode allows handle interaction.
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual void CapturedMouseMove(FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
	virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override;

	// NOTE: We considered overriding `FEditorViewportClient::OverridePostProcessSettings(FSceneView&)`
	// for DoF injection, but that virtual hook only fires when
	// `bUseControllingActorViewInfo == false` (see
	// `EditorViewportClient.cpp:1444-1460`). We need bUseControllingActorViewInfo
	// = true for the MaintainXFOV aspect override, so the virtual is dead
	// code in our setup. Instead, DoF is written directly into
	// `ControllingActorViewInfo.PostProcessSettings` inside
	// `RunSolverAndDriveCamera` - the engine reads that when
	// bUseControllingActorViewInfo is true (line 1448 of the engine
	// source above).

private:
	/** Re-spawn the proxy set for the current ActiveShot's Targets. Despawns
	 * any existing proxies first. Safe to call when ActiveShot is null. */
	void RebuildProxies();

	/** Despawn every proxy actor and clear the array. */
	void DestroyProxies();

	/** Per-frame transform copy from source actor->proxy. Skips null entries. */
	void SyncProxyTransforms();

	/** Run SolveShot and write the result onto the viewport's camera.
	 * `DeltaSeconds` flows into the framing-zone damping term; pass 0
	 * for instant-snap behavior (e.g. one-shot preview rebuilds). */
	void RunSolverAndDriveCamera(float DeltaSeconds);

	/** Spawn one proxy in the preview world for `SourceActor` or a template
	 * preview mesh. Picks live SK / live SM / preview SK / capsule fallback. */
	AActor* SpawnProxyForTarget(AActor* SourceActor,
		USkeletalMesh* PreviewMesh,
		const FTransform& PreviewTransform);

	/** Resolve the source actor for `Targets[TargetIndex]`. When the active
	 * host is a `UMovieSceneComposableCameraShotSection`, per-section
	 * TargetActorOverrides are consulted first - the override's
	 * FMovieSceneObjectBindingID is resolved against the open Sequencer for
	 * the section's owning sequence. Falls back to the Shot's authored
	 * runtime actor. Asset-only preview meshes are resolved separately by
	 * `ResolvePreviewMeshForTargetIndex`. */
	AActor* ResolveSourceActorForTargetIndex(int32 TargetIndex) const;

	/** Resolve editor-only SKM preview asset for a target. Used only when no
	 * live actor / LS binding resolves. */
	USkeletalMesh* ResolvePreviewMeshForTargetIndex(int32 TargetIndex) const;

	/** Resolve editor-only preview proxy transform for a target. */
	FTransform ResolvePreviewTransformForTargetIndex(int32 TargetIndex) const;

	/** Build a value-copy of `*ActiveShot` with each `Targets[i].Target.Actor`
	 * replaced by the override-resolved live actor (when an override exists
	 * and resolves) - see `ResolveSourceActorForTargetIndex`. The result is
	 * what the solver / read paths in this viewport consume so a Section's
	 * TargetActorOverrides actually drive the preview camera, not just the
	 * proxy spawn.
	 *
	 * Returns false (OutShot left unchanged) when ActiveShot is null.
	 *
	 * **Per-frame cache (Polish P.2)**: a single tick triggers 5+ calls
	 * (HUD draw, 3D wire BBs, handles, solver run, hover tooltip, etc.).
	 * Each call previously did a full `*ActiveShot` value copy AND ran
	 * per-target Sequencer-binding override resolution
	 * (`ResolveSourceActorForTargetIndex`, which queries open Sequencer
	 * instances). The override resolution dominates the cost. Cached on
	 * the viewport client and invalidated at the start of each `Tick`,
	 * so within one frame all callers share a single resolution pass.
	 * The struct copy still runs per-call (callers own `OutShot` and may
	 * mutate it - the bounds-cache refresh in `Draw()` does), but the
	 * TArray<FComposableCameraShotTarget> heap allocation is the only
	 * inherent per-call cost; the rest is memcpy-shape work. */
	bool BuildEffectiveShotForPreview(FComposableCameraShot& OutShot) const;

private:
	/** The PreviewScene the SEditorViewport gave us. NOT owned - owner is the
	 * SShotEditorViewport widget. */
	FPreviewScene* PreviewScene = nullptr;

	/** Active Shot pointer - raw, lives inside host UObject's UPROPERTY.
	 * Mirrors SShotEditorRoot's ActiveShot; null when no Shot bound. */
	FComposableCameraShot* ActiveShot = nullptr;

	/** Liveness guard for ActiveShot. */
	TWeakObjectPtr<UObject> ActiveHost;

	/** Snapshot value of the host pointer at last RebuildProxies call.
	 * Used to detect "host changed" and trigger rebuild even if Targets count
	 * matches by accident. */
	TWeakObjectPtr<UObject> LastRebuiltHost;

	/** One stand-in actor per Shot.Targets[i], in the same order. Weak refs
	 * let us detect proxies that were nulled out by the world (shouldn't
	 * happen in normal operation but cheap insurance). */
	TArray<TWeakObjectPtr<AActor>> ProxyActors;

	/** Per-target source actor at last RebuildProxies time. Used to detect
	 * when a designer assigns / changes / clears `Targets[i].Target.Actor`
	 * in the Details panel - the Targets array length doesn't change in
	 * that case, so the count-drift check alone misses it and the proxy
	 * stays stuck on its initial mesh choice (cylinder fallback for the
	 * null-actor case). Each Tick compares this snapshot against the
	 * current per-target resolved actors and forces a rebuild on mismatch. */
	TArray<TWeakObjectPtr<AActor>> LastResolvedSources;

	/** Per-target preview mesh at last RebuildProxies time. Tracked in
	 * parallel with LastResolvedSources so changing EditorPreviewMesh
	 * respawns the proxy even when no live Actor exists. */
	TArray<TWeakObjectPtr<USkeletalMesh>> LastResolvedPreviewMeshes;

	/** Per-frame cache for `BuildEffectiveShotForPreview` (Polish P.2).
	 * Holds the most recent successful effective-shot computation; reused
	 * by every paint-time caller within one tick. Invalidated at the
	 * start of each `Tick(DeltaSeconds)`. `mutable` so the const-method
	 * `BuildEffectiveShotForPreview` can populate it lazily on first call
	 * per frame.
	 *
	 * `bEffectiveShotCacheValid` is the freshness bit. `bEffectiveShotCacheBuiltOk`
	 * records whether the last successful build returned true (cache holds
	 * a real shot) vs false (ActiveShot was null, cache contents undefined).
	 * Both default-cleared on Tick. */
	mutable FComposableCameraShot CachedEffectiveShot;
	mutable bool bEffectiveShotCacheValid = false;
	mutable bool bEffectiveShotCacheBuiltOk = false;

	/** Mode set by the Shot Editor toolbar. Drag (default) = solver-driven
	 * + interactive handles; Free = user-camera + handles follow live world
	 * projection (non-interactive); Lock = solver-driven + all input consumed
	 * (read-only preview). */
	EShotEditorMode CurrentMode = EShotEditorMode::Drag;

	/** Top-left camera / aspect / focus text overlay. */
	bool bShowDiagnosticHud = true;

	/** Screen handles, framing zones, and target bounds wireframes. */
	bool bShowCompositionGuides = true;

	// D.4 Handle drag state 

	/** Type of handle being drawn / hit-tested / dragged. Two handles - one
	 * per anchor screen
	 * position (Placement.ScreenPosition + Aim.ScreenPosition). Per-target
	 * handles dropped (no more per-target screen-position UPROPERTY). */
	enum class EHandleType: uint8
	{
		None,
		PlacementAnchor, // Shot.Placement.ScreenPosition
		AimAnchor // Shot.Aim.ScreenPosition
	};

	/** Cache populated each DrawCanvas frame for hit-testing in InputKey /
	 * MouseMove. Pixel coords are in viewport-local space (origin top-left,
	 * +Y down).
	 *
	 * Two cached-entry kinds:
	 * - Anchor - the LMB-grab disc for `Placement.ScreenPosition` /
	 * `Aim.ScreenPosition`. `HitArea` is a small box around
	 * `PixelPos`; `bIsZoneEdge` is false; zone-edge fields
	 * are unused.
	 * - ZoneEdge - one of the four edges of an enabled framing-zone
	 * rectangle (dead OR soft x L/R/T/B). `HitArea` is a
	 * thin rect aligned with the edge; `bIsZoneEdge` is
	 * true; zone-edge fields identify which size to mutate.
	 */
	struct FHandleScreenPosCache
	{
		/** Center point of the handle (anchor disc center, or midpoint of
		 * the edge). Used for "follow cursor" rendering during an active
		 * drag and for tooltip anchoring. */
		FVector2D PixelPos;

		/** Hit-test rectangle. Anchor: small square around PixelPos. Zone
		 * edge: thin strip along the edge with `kZoneEdgeHitThickness`
		 * perpendicular thickness. Hit test reduces to point-in-box. */
		FBox2D HitArea { ForceInit };

		/** Anchor this handle belongs to. For Anchor entries this is also
		 * the dragged target; for ZoneEdge entries the edge mutates the
		 * *zones* attached to this anchor (Placement -> `PlacementZones`,
		 * Aim -> `AimZones`). */
		EHandleType Type;

		// Zone-edge specifics - only valid when bIsZoneEdge 

		bool bIsZoneEdge = false;

		/** false = `DeadZoneSize` edge, true = `SoftZoneSize` edge. */
		bool bIsSoftZone = false;

		/** Which edge: 0=Left, 1=Right, 2=Top, 3=Bottom. Drives both the
		 * drag math (which axis of the zone size to mutate, and on which
		 * side of the anchor the cursor is expected) and the hit-area
		 * orientation (vertical vs horizontal strip). */
		int32 EdgeIndex = -1;
	};
	TArray<FHandleScreenPosCache> CachedHandles;

	/** Current drag state. ActiveDragHandleType == None when no drag in
	 * progress. When `bActiveDragIsZoneEdge` is true, the type identifies
	 * the *anchor whose zones are being edited*, and the edge / soft-vs-
	 * dead fields describe which size component the cursor mutates. */
	EHandleType ActiveDragHandleType = EHandleType::None;
	bool bActiveDragIsZoneEdge = false;
	bool ActiveDragZoneIsSoft = false;
	int32 ActiveDragZoneEdgeIndex = -1;
	TUniquePtr<FScopedTransaction> DragTransaction;

	/** Hover state for visual feedback (unused while a drag is active). */
	EHandleType HoveredHandleType = EHandleType::None;
	/** Mirror of `ActiveDragIsZoneEdge` / etc. for hover - drives whether
	 * hovering an edge highlights it (color + thickness) without firing a
	 * drag. */
	bool bHoveredIsZoneEdge = false;
	bool HoveredZoneIsSoft = false;
	int32 HoveredZoneEdgeIndex = -1;

	// Alt+RMB Roll drag state (Drag + Free modes)
	//
	// Alt+RMB-drag rotates `Shot.Roll` (NOT view-rotation Roll) so the
	// gesture round-trips through Modify / PostEditChangeProperty (ValueSet)
	// - Ctrl+Z restores the prior Roll. Drag mode picks up the change via
	// the per-tick solver (which composes `Shot.Roll` into the camera
	// rotation); Free mode picks it up via the per-tick "view.Roll =
	// Shot.Roll" sync at the bottom of `RunSolverAndDriveCamera`.
	//
	// Anchor / target screen-position handle drags (LMB) and Roll drags
	// (Alt+RMB) use disjoint state and disjoint mouse buttons, so they
	// can't collide; CapturedMouseMove routes on whichever flag is set.
	bool bRollDragActive = false;

	/** Mouse X at the previous CapturedMouseMove sample, used to compute
	 * per-frame Roll delta. Captured at gesture start in `StartRollDrag`. */
	int32 RollDragLastMouseX = 0;

	/** Single-gesture transaction. Same SaveToTransactionBuffer-bypass-Modify
	 * pattern as the LMB handle drag (TechDoc Section 7.2 Sequencer-respawn flash):
	 * begin uses `SaveToTransactionBuffer(Host, false)` for the undo snapshot
	 * without firing OnObjectModified mid-gesture; commit fires
	 * `PostEditChangeProperty(ValueSet)`. */
	TUniquePtr<FScopedTransaction> RollTransaction;

	/** Hit-test cached handles against pixel coords, returns first match
	 * (handles drawn last are tested first->top-most hit wins). Returns
	 * the full cache entry so callers can distinguish anchor-drag from
	 * zone-edge-drag and route accordingly. */
	bool HitTestHandles(int32 PixelX, int32 PixelY,
		FHandleScreenPosCache& OutHit) const;

	/** Convert normalized screen [-0.5, 0.5] (our solver convention,
	 * +Y up) viewport pixel coords (top-left origin, +Y down). */
	FVector2D NormalizedScreenToPixel(const FVector2D& ScreenPos, const FIntPoint& VPSize) const;
	FVector2D PixelToNormalizedScreen(int32 PixelX, int32 PixelY, const FIntPoint& VPSize) const;

	/** Draw all handles for the active Shot. Anchor + non-anchor target
	 * with weight > 0. Greyed out in Manual Mode. Populates CachedHandles
	 * for hit-testing. */
	void DrawHandles(FViewport& InViewport, FCanvas& Canvas);


	/** Apply mouse motion to the active drag - writes Shot screen position
	 * + fires Host PostEditChangeProperty(Interactive). */
	void ApplyDragToShot(int32 PixelX, int32 PixelY);

	/** Commit drag: PostEditChangeProperty(ValueSet), drop transaction,
	 * clear drag state. */
	void EndDrag();

	/**
	 * Begin an Alt+RMB Roll drag. Snapshots the host UObject for undo via
	 * SaveToTransactionBuffer (no OnObjectModified broadcast - same flash
	 * mitigation as the LMB handle drag), captures the current mouse X for
	 * delta computation, and flips `bRollDragActive`. No-op when already
	 * dragging or when there's no active Shot / viewport.
	 */
	void StartRollDrag();

	/**
	 * Apply a CapturedMouseMove sample to the in-flight Roll drag.
	 * Computes `DeltaX = PixelX - RollDragLastMouseX`, accumulates
	 * `0.5 deg * DeltaX` into `Shot.Roll`, wraps via `UnwindDegrees`, and
	 * updates `RollDragLastMouseX`. No `PostEditChangeProperty(Interactive)`
	 * mid-gesture - `EndRollDrag`'s ValueSet notification carries the
	 * commit, matching the LMB handle drag's deferred-notify pattern.
	 */
	void ApplyRollDrag(int32 PixelX);

	/**
	 * Commit the Alt+RMB Roll drag. Fires `PostEditChangeProperty(ValueSet)`
	 * on the host's `Shot` UPROPERTY (so Details panel + listeners react),
	 * drops the transaction (closing the undo entry), clears the gesture
	 * flag.
	 */
	void EndRollDrag();

	/**
	 * Drag-mode mouse-wheel handler: each scroll click multiplies
	 * `Shot.Placement.Distance` by `1 / ZoomFactor` (scroll up->closer)
	 * or `ZoomFactor` (scroll down->further). Scoped to placement modes
	 * that actually read `Distance` - `AnchorOrbit` and `AnchorAtScreen`.
	 * `FixedWorldPosition` ignores the scroll (returns false) so the
	 * caller can fall through to the general "eat all mouse buttons"
	 * guard in Drag-mode `InputKey`.
	 *
	 * Each scroll click commits as one undo entry via `FScopedTransaction`
	 * + `Host->Modify` + `PostEditChangeProperty(ValueSet)`. Returns true
	 * iff a write actually happened (mode supports Distance AND the
	 * clamped new value differs from the old).
	 */
	bool TryAdjustDistanceFromMouseWheel(bool bScrollUp);

	// No RMB context menu on anchor handles. Anchors do not carry bones; bone
	// authoring lives on the per-target Details combo
	// (`FComposableCameraTargetInfoCustomization`). RMB on the viewport falls
	// through to base class behavior in Free mode and is eaten in Drag/Lock.

	/** Cached solver-output focus distance (cm). Pushed into the SceneView's
	 * `FinalPostProcessSettings.DepthOfFieldFocalDistance` each frame in
	 * `OverridePostProcessSettings`. */
	float CachedFocusDistance = 200.f;

	/** Cached solver-output aperture (f-stop). Pushed into the SceneView's
	 * `FinalPostProcessSettings.DepthOfFieldFstop` each frame. */
	float CachedAperture = 2.8f;

	/** True when CachedFocusDistance / CachedAperture were populated by a
	 * successful solve. Cleared while no Shot is bound or the solver
	 * returned `bValid == false`; OverridePostProcessSettings reads this
	 * to avoid pushing stale values during transient unresolvable shots. */
	bool bCachedDoFValid = false;

	// Cinemachine-style framing-zone prior-pose cache 
	//
	// Editor preview parallels the runtime FramingNode's prior-pose state
	// (see `UComposableCameraCompositionFramingNode::LastPrimaryOutputPose`).
	// On the first valid solve after binding a Shot, the cache is seeded;
	// subsequent ticks pass it as the `FShotPriorPose` argument to
	// `SolveShot` so designers see the same Cinemachine-style damped
	// framing in the Shot Editor as in PIE / runtime.
	//
	// Only Position + Rotation are needed - the zone preprocessor
	// re-projects anchors using the current frame's TanHalfHOR / aspect.

	/** True iff `CachedPriorPos` / `CachedPriorRot` carry a usable prior
	 * solve. Cleared on Shot rebind / dissolution; first valid solve
	 * after the clear seeds them. */
	bool bHasCachedPriorPose = false;
	FVector CachedPriorPos = FVector::ZeroVector;
	FRotator CachedPriorRot = FRotator::ZeroRotator;
	/** Last frame's effective `Placement.Distance` (post-damping + clamp).
	 * `< 0` no prior; solver skips Distance damping. */
	float CachedPriorDistance = -1.f;
	/** Last frame's effective FOV / Roll (post-damping). Sentinels
	 * match `FShotPriorPose::LastFOV` (`< 0`) and `LastRoll` (`FLT_MAX`). */
	float CachedPriorFOV = -1.f;
	float CachedPriorRoll = TNumericLimits<float>::Max();
};
