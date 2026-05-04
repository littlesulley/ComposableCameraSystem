// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "CineCameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MovieScene/MovieSceneComposableCameraPatchSection.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "Utils/ComposableCameraViewportUtils.h"

UComposableCameraLevelSequenceComponent::UComposableCameraLevelSequenceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Default off: can be flipped by a future ECS instantiator for strict
	// cut/blend-only gating. V1.4's simplified path auto-enables from
	// OnRegister, so in practice every spawned LS actor ticks.
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Editor preview & Sequencer scrubbing happen in editor-world tick;
	// SetComponentTickEnabled(true) within editor world respects this flag.
	bTickInEditor = true;

	// OutputCineCameraComponent is provided by the owning Actor:
	//   - AComposableCameraLevelSequenceActor assigns it during construction
	//     (the CineCamera is the Actor's RootComponent, created as a
	//     DefaultSubobject at the Actor level).
	//   - If the component is added to an arbitrary Actor (BlueprintSpawnableComponent),
	//     OnRegister falls back to FindComponentByClass on the owning Actor.
}

void UComposableCameraLevelSequenceComponent::OnRegister()
{
	Super::OnRegister();

	// Fallback CineCamera lookup for arbitrary-Actor hosts (BlueprintSpawnableComponent).
	// AComposableCameraLevelSequenceActor pre-assigns OutputCineCameraComponent
	// in its constructor; for other hosts, find the first CineCamera on the
	// owning Actor. If none is found, projection is a no-op — the component
	// still evaluates, it just has no viewport terminal.
	if (!OutputCineCameraComponent)
	{
		if (AActor* Owner = GetOwner())
		{
			OutputCineCameraComponent = Owner->FindComponentByClass<UCineCameraComponent>();
		}
	}

	// Keep the bags in sync with the TypeAsset on first registration — handles
	// loading a saved component whose TypeAsset interface has changed since the
	// last save (e.g. someone added a new ExposedParameter). MigrateToNewBagStruct
	// preserves existing values for surviving names + types.
	TypeAssetReference.RebuildBagsFromTypeAsset();

	// Evaluation gate semantics: unconditionally ON by default. The ECS gate
	// (UMovieSceneComposableCameraGateInstantiator) does not "open" the gate —
	// it CLOSES it, turning inactive components off for tracked Spawnables
	// whose owning actor isn't the Camera Cut Track's current target or a
	// blend participant. Entities it cannot reach (non-Sequencer hosts like
	// BlueprintSpawnableComponent on a plain actor) stay at the default (on).
	//
	// First-frame setup-then-teardown: for a gated entity that isn't in the
	// Camera Cut Track's current target the first time the instantiator sees
	// it, OnRegister enables → tick runs briefly → instantiator flips to false
	// on its next OnRun. Visually imperceptible; avoids the "component goes
	// silent forever" trap a default-off model creates.
	SetEvaluationEnabled(true);
}

void UComposableCameraLevelSequenceComponent::BeginPlay()
{
	Super::BeginPlay();
	// InternalCamera is created lazily on the first tick that actually needs it,
	// so a dormant LS actor (Spawnable inactive, or Phase C "not participating")
	// pays no camera-construction cost.
}

void UComposableCameraLevelSequenceComponent::OnUnregister()
{
	// Editor-world counterpart to EndPlay. Sequencer Spawnable actors live in
	// the editor preview world (and in various transient worlds during asset
	// save / cook). Those worlds never invoke BeginPlay → the actor's EndPlay
	// is never called → our EndPlay override never runs → InternalCamera
	// leaks every time the Spawnable is destroyed and re-created. OnRegister /
	// OnUnregister fire reliably in both the editor and PIE, so cleaning up
	// here guarantees pairing.
	//
	// In PIE / Standalone, OnUnregister fires alongside the actor teardown
	// that also triggers EndPlay. The second DestroyInternalCamera is a no-op
	// because InternalCamera is already null. No double-free risk.
	DestroyInternalCamera();
	Super::OnUnregister();
}

void UComposableCameraLevelSequenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyInternalCamera();
	Super::EndPlay(EndPlayReason);
}

void UComposableCameraLevelSequenceComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEvaluationEnabled)
	{
		return;
	}

	// Per-frame diagnostic — enable with `Log LogComposableCameraSystem Verbose`
	// to see exactly which Spawnables are ticking each frame. Noisy at Verbose;
	// default Log level filters this out.
	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("LS tick [%s]"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));

	EvaluateOnce(DeltaTime);
}

void UComposableCameraLevelSequenceComponent::EvaluateOnce(float DeltaTime)
{
	if (!TypeAssetReference.TypeAsset)
	{
		return;
	}

	EnsureInternalCamera();

	if (!InternalCamera)
	{
		return;
	}

	// Phase D: re-sync the bag's current values into the runtime data block
	// before ticking. Sequencer's stock FPropertyTrack evaluation writes
	// directly into the bag's backing FProperty each frame; our job is to
	// propagate those writes into the camera's data block so node tick
	// reads see the animated value. ApplyParameterBlock handles enum
	// narrow-cast / struct copy / every pin type correctly — same path
	// that's used at activation — so re-calling it each frame is safe and
	// idempotent for values that didn't change.
	//
	// Cost is O(number of exposed parameters) per frame, bounded by the
	// TypeAsset's surface (typically <20). No allocations on the hot path
	// beyond the parameter block scratch struct itself.
	if (InternalCamera->OwnedRuntimeDataBlock)
	{
		FComposableCameraParameterBlock Block;
		TypeAssetReference.BuildParameterBlock(Block);
		TypeAssetReference.TypeAsset->ApplyParameterBlock(*InternalCamera->OwnedRuntimeDataBlock, Block);
	}

	// Phase E: apply Sequencer Shot override BEFORE TickCamera so the
	// CompositionFramingNode's solver sees the new Shot data on the same
	// frame. No-op when the override map is empty — CompositionFramingNode
	// keeps its last-written Shot.
	if (SequencerShotOverrides.Num() > 0)
	{
		ApplyActiveSequencerShotOverride();
	}

	// Note: DeltaTime in editor-world scrubbing is synthetic (Sequencer drives
	// it) — history-dependent nodes may look off during scrub. Phase B accepts
	// this; see TechDoc §7 gotcha for history-dependent nodes. PIE DeltaTime is
	// real frame-to-frame time and nodes behave normally.
	FComposableCameraPose Pose = InternalCamera->TickCamera(DeltaTime);

	// Sequencer patch overlay pass — runs in all worlds (Editor preview AND
	// PIE / Game / Standalone). Patches added through the BP library
	// `AddCameraPatch` go through a separate gameplay PCM/Director path; this
	// LS Component path handles ONLY Sequencer-driven patches whose section
	// has TargetActorBinding set to this actor. Applied AFTER InternalCamera's
	// tick + BEFORE projection so patches see the LS Component's clean base
	// pose as upstream and the CineCamera receives the final patched pose.
	// This intentionally also writes FOV (CineCamera's optics are touched
	// here) so DollyZoom-style patches preview correctly — the projection
	// path proper still leaves optics alone for the non-overlay case.
	//
	// Whether this LS Component ticks at all is gated by the ECS gate
	// (UMovieSceneComposableCameraGateInstantiator) — if the LS Actor isn't
	// the active Camera Cut Track target, the gate closes us and patches
	// don't fire. That's the right semantic: Sequencer patches are visible
	// only while their host LS Actor is the camera target.
	if (SequencerPatchOverlays.Num() > 0)
	{
		ApplySequencerPatchOverlays(Pose, DeltaTime);
	}

	ProjectPoseToCineCamera(Pose);
}

#if WITH_EDITOR
void UComposableCameraLevelSequenceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName   = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	// Swapping the TypeAsset or rewiring the reference struct entirely: rebuild
	// both bags (names / types may have shifted) and spawn a fresh internal
	// camera so the Details-panel change is reflected in the viewport immediately.
	const bool bIsReferenceEdit =
		MemberName == GET_MEMBER_NAME_CHECKED(UComposableCameraLevelSequenceComponent, TypeAssetReference);

	const bool bIsTypeAssetEdit =
		PropertyName == GET_MEMBER_NAME_CHECKED(FComposableCameraTypeAssetReference, TypeAsset);

	if (bIsReferenceEdit || bIsTypeAssetEdit)
	{
		// TypeAsset swap: bag layout may change, exposed parameter set may
		// change, the whole camera-construction result could shift. Rebuild
		// from scratch.
		TypeAssetReference.RebuildBagsFromTypeAsset();
		RebuildInternalCamera();
		return;
	}

	// Bag value edits (Parameters / Variables): Phase D no longer rebuilds
	// the camera. The next TickComponent re-applies the bag via
	// ApplyParameterBlock, which is O(exposed-parameter count) and cheap
	// enough to pay once per frame. This also matches the Sequencer-driven
	// path: when a property track writes to the bag's backing field, the
	// same per-tick re-apply picks it up automatically — no special hook.
}
#endif

void UComposableCameraLevelSequenceComponent::SetEvaluationEnabled(bool bEnabled)
{
	const bool bWasEnabled = bEvaluationEnabled;

	if (bWasEnabled != bEnabled)
	{
		// Diagnostic: log every gate transition with the owning actor's name
		// so you can follow which Spawnables the ECS gate is actually closing.
		// Enable with `Log LogComposableCameraSystem Log` (default) — these
		// fire only on change, so they're low-noise.
		UE_LOG(LogComposableCameraSystem, Log,
			TEXT("LS gate [%s]: %s → %s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"),
			bWasEnabled ? TEXT("ON") : TEXT("OFF"),
			bEnabled ? TEXT("ON") : TEXT("OFF"));
	}

	bEvaluationEnabled = bEnabled;
	SetComponentTickEnabled(bEnabled);
	if (!bEnabled)
	{
		// Drop the internal camera when told to stop evaluating. Phase B keeps
		// this simple; Phase C may prefer to keep the camera alive (cheaper
		// wake-up) depending on how aggressive the gating proves to be in
		// practice.
		DestroyInternalCamera();
		return;
	}

	// Gate just opened (OFF → ON). Force a full evaluation pass right now,
	// before any Camera Cut switches the PCM ViewTarget to this LS Actor's
	// CineCamera and the renderer captures its transform. Without this,
	// the standard `TickComponent` runs after the first PCM render and
	// the LS Actor's CineCamera renders one frame at its default (origin)
	// transform on every cut into a non-overlapping section — visually
	// "camera at world origin for one frame". Cuts INTO an already-active
	// LS Actor (overlap case) are unaffected because the LS Component has
	// been ticking continuously, so its CineCamera transform is already
	// valid when the cut fires.
	//
	// `DeltaTime = 0` snaps V2.2 IIR damping (Distance / FOV / Roll) to
	// the authored value — exactly the cut-as-cut semantic the rest of
	// the V2.2 Section-transition handling already enforces.
	if (bEnabled && !bWasEnabled)
	{
		EvaluateOnce(0.f);
	}
}

void UComposableCameraLevelSequenceComponent::SetParameterValue(
	FName Name, const void* Value, EComposableCameraPinType Type)
{
	// Phase D simplified path does not use these API hooks. Parameters are
	// driven by writing directly to the bag (either by Sequencer's stock
	// property-track evaluation or by the Details panel), and the per-tick
	// ApplyParameterBlock pulls the current bag values into the camera's
	// runtime data block automatically.
	//
	// The hooks remain here as a forward-compatibility surface: if a future
	// ECS Instantiator is added (for strict cut/blend gating or pre-animated
	// restore), this is where it would write through. Left empty on purpose.
	(void)Name; (void)Value; (void)Type;
}

void UComposableCameraLevelSequenceComponent::SetVariableValue(
	FName Name, const void* Value, EComposableCameraPinType Type)
{
	// Same as SetParameterValue — forward-compatibility stub for a future
	// ECS Instantiator. Variables follow the exact same bag → data block
	// path as parameters (they share the channel through ApplyParameterBlock).
	(void)Name; (void)Value; (void)Type;
}

void UComposableCameraLevelSequenceComponent::EnsureInternalCamera()
{
	if (InternalCamera)
	{
		return;
	}

	UWorld* World = GetWorld();
	UComposableCameraTypeAsset* TypeAsset = TypeAssetReference.TypeAsset;
	if (!World || !TypeAsset)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// No replication / input — this actor is a pure local evaluator.
	SpawnParams.bNoFail = true;

	// Spawn at the owning Actor's transform. We're a UActorComponent (no
	// transform of our own); use the Actor as the anchor. The exact spawn
	// transform doesn't matter much — nodes typically overwrite the pose
	// every tick via TickCamera — but starting near the Actor avoids the
	// camera being briefly visible at the world origin on the first frame.
	const FTransform SpawnTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
	InternalCamera = World->SpawnActor<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(),
		SpawnTransform,
		SpawnParams);

	if (!InternalCamera)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("UComposableCameraLevelSequenceComponent::EnsureInternalCamera: "
			     "Failed to spawn internal camera for TypeAsset '%s'."),
			*TypeAsset->GetName());
		return;
	}

	// Note: SpawnActor above has already run AActor::BeginPlay → BeginPlayCamera
	// with empty CameraNodes / ComputeNodes, so the compute-chain walk was a
	// harmless no-op. That is intentional: we skip the compute chain entirely
	// in the LS path (compute nodes return GetLevelSequenceCompatibility() ==
	// ComputeOnly; see Phase F warning). Any subsequent TickCamera walks
	// CameraNodes only, which is exactly what we want.

	// The internal camera is driven by our TickComponent via explicit TickCamera
	// calls; its own actor tick has no work and would just cost a scheduler
	// visit per frame. Suppress it.
	InternalCamera->SetActorTickEnabled(false);

	// Initialize with PCM = nullptr. Phase A made this path valid: the camera
	// base null-guards its BindCameraActionsForNewCamera call, and nodes that
	// unconditionally require a PCM either (a) early-return in OnInitialize
	// and OnTickNode or (b) are categorically ComputeOnly (skipped) — see
	// GetLevelSequenceCompatibility overrides on ScreenSpace*, MixingCamera,
	// and the compute-node base.
	InternalCamera->Initialize(/*Manager=*/nullptr);

	FComposableCameraParameterBlock Block;
	TypeAssetReference.BuildParameterBlock(Block);

	UE::ComposableCameras::ConstructCameraFromTypeAsset(InternalCamera, TypeAsset, Block);
}

void UComposableCameraLevelSequenceComponent::RebuildInternalCamera()
{
	DestroyInternalCamera();
	EnsureInternalCamera();
}

void UComposableCameraLevelSequenceComponent::NotifyTypeAssetExternallyChanged()
{
	TypeAssetReference.RebuildBagsFromTypeAsset();
	RebuildInternalCamera();
}

void UComposableCameraLevelSequenceComponent::DestroyInternalCamera()
{
	if (InternalCamera && IsValid(InternalCamera))
	{
		InternalCamera->Destroy();
	}
	InternalCamera = nullptr;

	// Mirror evaluator cleanup for editor-preview patch overlays — the LS
	// Component spawned these as transient actors, so when the component
	// itself is being torn down (typical caller paths: OnUnregister /
	// EndPlay → DestroyInternalCamera) those evaluators must go too. Without
	// this, Sequencer Save / spawnable re-spawn / hot-reload would leak one
	// evaluator per registered patch section per teardown cycle.
	for (TPair<TObjectPtr<UMovieSceneComposableCameraPatchSection>, FComposableCameraSequencerPatchOverlay>& Pair : SequencerPatchOverlays)
	{
		if (Pair.Value.Evaluator && IsValid(Pair.Value.Evaluator))
		{
			Pair.Value.Evaluator->Destroy();
		}
	}
	SequencerPatchOverlays.Reset();

	// Shot overrides hold no spawned actors — they're pure data — so a plain
	// Reset is sufficient. The CompositionFramingNode that consumed the
	// last-written Shot lives on the InternalCamera that we just destroyed
	// above; on the next EnsureInternalCamera the framing node is fresh
	// (default-constructed Shot) until a new override fires.
	SequencerShotOverrides.Reset();
}

void UComposableCameraLevelSequenceComponent::ProjectPoseToCineCamera(const FComposableCameraPose& Pose)
{
	if (!OutputCineCameraComponent)
	{
		return;
	}

	// Framing solver failed this tick — hold the CineCamera's current
	// transform instead of writing the upstream-default pose (which is
	// `(0, 0, 0)` for a freshly-spawned InternalCamera and would render
	// the camera at world origin). Hits the gate-flip-ON edge case: the
	// synchronous `EvaluateOnce(0)` in `SetEvaluationEnabled(true)` runs
	// in the instantiation phase, BEFORE the Shot TrackInstance pushes
	// its first override in the evaluation phase, so the framing node
	// solves against its default empty Shot and fails. Without this
	// guard, the failure would burn an origin pose onto the CineCam
	// before the second `EvaluateOnce` (triggered by `SetSequencerShotOverride`'s
	// first-entry re-eval) can write the correct one, and the PCM POV
	// captured between the two writes (CameraCut TrackInstance fires in
	// evaluation phase too — order vs. ours is undefined) would render
	// the actor at world origin for a frame.
	//
	// Cameras without a `CompositionFramingNode` never set this flag
	// (default `false`), so the projection runs unconditionally for them
	// — preserves the original behavior for non-Shot-driven LS Actors.
	if (InternalCamera && InternalCamera->bLastTickFramingFailed)
	{
		return;
	}

	// Position + Rotation always — the unconditional baseline that the
	// camera-track / cut path relies on regardless of whether a Shot
	// Section is driving the LS Component this frame.
	OutputCineCameraComponent->SetWorldLocationAndRotation(Pose.Position, Pose.Rotation);

	// Phase E + Polish: Lens fields (FOV / Aperture / FocusDistance)
	// project onto the CineCamera ONLY when a Sequencer Shot Section is
	// actively overriding the framing this frame — i.e.
	// `SequencerShotOverrides.Num() > 0`. The pose then carries the
	// solver's resolved Lens.* values from the active Shot's
	// `FShotPlacement` / `FShotLens` / `FShotFocus` authoring (per
	// `ApplySolverResultToPose` in `CompositionFramingNode.cpp`), and
	// the user's Shot Editor edits flow through to the rendered camera.
	//
	// Outside Section ranges the LS Component still ticks (per
	// `SetEvaluationEnabled` semantics), and `CompositionFramingNode`
	// still produces a pose with `PhysicalCameraBlendWeight = 1` from
	// the node's default-authored Shot — but those Lens values are
	// meaningless context (no Section asked for them) and writing them
	// here would clobber the designer-authored CineCamera optics
	// (recently made editable via the `OutputCineCameraComponent`
	// UPROPERTY). The Section-active gate keeps the original Phase B
	// invariant — "free-standing LSActors don't steal CineCam optic
	// authoring" — while the V2 Shot system gets its expected
	// "Shot.Lens flows to CineCam" behaviour.
	//
	// Patch overlay path (`ApplySequencerPatchOverlays` above) writes a
	// FOV separately for DollyZoom etc.; that path still works because
	// it runs BEFORE `ProjectPoseToCineCamera` and modifies `Pose` in
	// place — the FOV we read here is already the patched one.
	const bool bSectionDriven = (SequencerShotOverrides.Num() > 0);
	if (bSectionDriven && Pose.PhysicalCameraBlendWeight > 0.f)
	{
		// FOV: dual-mode resolution per invariant #15. `FocalLength < 0`
		// is the sentinel `CompositionFramingNode::ApplySolverResultToPose`
		// writes after a successful solve, signalling "use FieldOfView,
		// not FocalLength". `SetFieldOfView` updates `CurrentFocalLength`
		// to match the FOV given the current Filmback (non-destructive
		// to authored Filmback / sensor settings).
		if (Pose.FieldOfView > 0.f && Pose.FocalLength < 0.f)
		{
			OutputCineCameraComponent->SetFieldOfView(Pose.FieldOfView);
		}
		else if (Pose.FocalLength > 0.f)
		{
			OutputCineCameraComponent->CurrentFocalLength = Pose.FocalLength;
		}

		if (Pose.Aperture > 0.f)
		{
			OutputCineCameraComponent->CurrentAperture = Pose.Aperture;
		}

		if (Pose.FocusDistance > 0.f)
		{
			// Update only the manual focus distance — leave `FocusMethod`
			// alone so a CineCam authored in `Tracking` mode keeps its
			// tracking-target authoring. When CineCam is in `Manual`
			// mode the value flows through directly; in `Tracking` mode
			// the CineCam ignores `ManualFocusDistance` and our write is
			// inert (Tracking math wins, designer's choice). In `Disable`
			// mode our write also silently no-ops at render time, which
			// is the intended outcome: designer turned focus off, Shot's
			// computed FocusDistance is irrelevant.
			OutputCineCameraComponent->FocusSettings.ManualFocusDistance = Pose.FocusDistance;
		}
	}
}

void UComposableCameraLevelSequenceComponent::SetSequencerPatchOverlay(
	UMovieSceneComposableCameraPatchSection* Section,
	const FComposableCameraParameterBlock& Parameters,
	float EnvelopeAlpha)
{
	if (!Section || !Section->PatchAsset)
	{
		return;
	}

	FComposableCameraSequencerPatchOverlay& Overlay = SequencerPatchOverlays.FindOrAdd(Section);

	// Lazy-spawn the evaluator on first use. PCM-independent construction
	// (Initialize(nullptr)) — same path PatchManager uses for runtime patch
	// evaluators, same path the LS Component uses for its own InternalCamera.
	// Reuse across frames; destroyed on RemoveSequencerPatchOverlay.
	if (!Overlay.Evaluator || !IsValid(Overlay.Evaluator))
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			SequencerPatchOverlays.Remove(Section);
			return;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = GetOwner();
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.bNoFail = true;

		AComposableCameraCameraBase* NewEvaluator = World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(),
			GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity,
			SpawnParams);

		if (!NewEvaluator)
		{
			SequencerPatchOverlays.Remove(Section);
			return;
		}

		// Suppress actor tick — TickComponent drives ticks via TickWithInputPose.
		NewEvaluator->SetActorTickEnabled(false);
		// PCM-independent init (skips BindCameraActionsForNewCamera).
		NewEvaluator->Initialize(/*Manager=*/nullptr);

		// Build evaluator from PatchAsset with the initial parameters.
		UE::ComposableCameras::ConstructCameraFromTypeAsset(NewEvaluator, Section->PatchAsset, Parameters);
		Overlay.Evaluator = NewEvaluator;
	}

	Overlay.LatestParameters = Parameters;
	Overlay.Alpha = FMath::Clamp(EnvelopeAlpha, 0.f, 1.f);
}

void UComposableCameraLevelSequenceComponent::RemoveSequencerPatchOverlay(
	UMovieSceneComposableCameraPatchSection* Section)
{
	if (!Section)
	{
		return;
	}

	FComposableCameraSequencerPatchOverlay* Found = SequencerPatchOverlays.Find(Section);
	if (!Found)
	{
		return;
	}

	if (Found->Evaluator && IsValid(Found->Evaluator))
	{
		Found->Evaluator->Destroy();
	}
	SequencerPatchOverlays.Remove(Section);
}

void UComposableCameraLevelSequenceComponent::ApplySequencerPatchOverlays(
	FComposableCameraPose& InOutPose, float DeltaTime)
{
	// Stable iteration order: snapshot the keys, sort by the section's resolved
	// LayerIndex (asset default OR per-call override on Params), then walk.
	// Sorting matches the runtime PatchManager's ActivePatches ordering so
	// editor preview composition is deterministic and consistent with PIE.
	// Use raw pointers for the local sorted array — `TArray<TObjectPtr>::Sort`
	// goes through TDereferenceWrapper which constructs `TObjectPtr` from a
	// reference (deprecated in UE 5.6, removed in next release).
	TArray<UMovieSceneComposableCameraPatchSection*> SortedKeys;
	SortedKeys.Reserve(SequencerPatchOverlays.Num());
	for (const TPair<TObjectPtr<UMovieSceneComposableCameraPatchSection>, FComposableCameraSequencerPatchOverlay>& Pair : SequencerPatchOverlays)
	{
		SortedKeys.Add(Pair.Key);
	}
	SortedKeys.Sort([](const UMovieSceneComposableCameraPatchSection& A,
	                   const UMovieSceneComposableCameraPatchSection& B)
	{
		auto LayerOf = [](const UMovieSceneComposableCameraPatchSection& S) -> int32
		{
			if (!S.PatchAsset) return 0;
			return S.Params.bOverrideLayerIndex ? S.Params.LayerIndex : S.PatchAsset->DefaultLayerIndex;
		};
		return LayerOf(A) < LayerOf(B);
	});

	bool bAnyOverlayApplied = false;

	// Stale-entry collection — sections that have been GC'd or whose evaluator
	// is invalid. Cleaned up after the walk to avoid mid-iteration map mutation.
	TArray<UMovieSceneComposableCameraPatchSection*> ToPrune;

	for (UMovieSceneComposableCameraPatchSection* Key : SortedKeys)
	{
		FComposableCameraSequencerPatchOverlay* Overlay = SequencerPatchOverlays.Find(Key);
		if (!Overlay) { continue; }
		if (!Key || !IsValid(Key) || !Overlay->Evaluator || !IsValid(Overlay->Evaluator))
		{
			ToPrune.Add(Key);
			continue;
		}
		if (Overlay->Alpha <= 0.f)
		{
			// Section is in-range but envelope says no contribution this frame
			// (e.g. exact section start before any ease has elapsed). Skip blend
			// to avoid a no-op BlendBy round-trip; the LatestParameters were
			// still cached so apply on the evaluator's runtime data block so
			// the next non-zero-alpha frame sees up-to-date values.
			if (UComposableCameraPatchTypeAsset* Asset = Key->PatchAsset)
			{
				if (Overlay->Evaluator->OwnedRuntimeDataBlock)
				{
					Asset->ApplyParameterBlock(*Overlay->Evaluator->OwnedRuntimeDataBlock, Overlay->LatestParameters);
				}
			}
			continue;
		}

		// Apply latest parameters to the evaluator's runtime data block before
		// ticking — same path PatchManager::ApplyParameterBlockToActivePatch
		// uses, just inlined since we own the evaluator directly here.
		if (UComposableCameraPatchTypeAsset* Asset = Key->PatchAsset)
		{
			if (Overlay->Evaluator->OwnedRuntimeDataBlock)
			{
				Asset->ApplyParameterBlock(*Overlay->Evaluator->OwnedRuntimeDataBlock, Overlay->LatestParameters);
			}
		}

		// Tick patch evaluator with the running pose as upstream → blended
		// pose lerps toward evaluator's output by alpha. Same shape as
		// runtime PatchManager::Apply but stateless on the envelope side.
		const FComposableCameraPose Evaluated = Overlay->Evaluator->TickWithInputPose(DeltaTime, InOutPose);
		InOutPose.BlendBy(Evaluated, Overlay->Alpha);
		bAnyOverlayApplied = true;
	}

	for (UMovieSceneComposableCameraPatchSection* Key : ToPrune)
	{
		FComposableCameraSequencerPatchOverlay* Found = SequencerPatchOverlays.Find(Key);
		if (Found && Found->Evaluator && IsValid(Found->Evaluator))
		{
			Found->Evaluator->Destroy();
		}
		SequencerPatchOverlays.Remove(Key);
	}

	// Patch-driven FOV writes need to land on the CineCamera too — the normal
	// ProjectPoseToCineCamera intentionally skips optics so designer / Sequencer
	// keys own them, but for overlay scenarios (DollyZoom etc.) we DO want the
	// patched FOV visible. Write FOV here (in addition to ProjectPoseToCineCamera's
	// later position+rotation write) so the CineCamera shows the patched optic.
	// `GetEffectiveFieldOfView` resolves dual-mode (FieldOfView vs FocalLength)
	// per invariant #15 in DesignDoc — gives degrees regardless of which mode
	// the pose is in.
	if (bAnyOverlayApplied && OutputCineCameraComponent)
	{
		const float TargetFOV = InOutPose.GetEffectiveFieldOfView();
		OutputCineCameraComponent->SetFieldOfView(TargetFOV);
		UE_LOG(LogComposableCameraSystem, Verbose,
			TEXT("LS '%s' applied %d patch overlay(s); wrote pos=%s, FOV=%.2f to CineCamera."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"),
			SortedKeys.Num(),
			*InOutPose.Position.ToCompactString(),
			TargetFOV);
	}
}

void UComposableCameraLevelSequenceComponent::SetSequencerShotOverride(
	UMovieSceneComposableCameraShotSection* Section,
	const FComposableCameraSequencerShotEntry& InEntry)
{
	if (!Section)
	{
		return;
	}

	// First-frame-of-section detection BEFORE FindOrAdd so we can tell a
	// genuinely-new override apart from a per-frame refresh of an existing one.
	const bool bIsNewEntry = (SequencerShotOverrides.Find(Section) == nullptr);

	// Whole-struct copy so the EnterTransition TObjectPtr inside the entry
	// is correctly tracked through TMap relocations (the prior field-by-field
	// assignment pattern was fine for a POD-y struct; once the struct holds
	// UPROPERTY-tracked TObjectPtrs, copy-assign is safer than partial writes).
	FComposableCameraSequencerShotEntry& Entry = SequencerShotOverrides.FindOrAdd(Section);
	Entry = InEntry;

	// Cut-into-Shot-driven-camera origin-frame fix.
	//
	// On a non-overlapping CameraCut into a gated LS Actor, the gate's
	// instantiation-phase OFF→ON flip fires `EvaluateOnce(0)` synchronously
	// (LS Component's `SetEvaluationEnabled` path) BEFORE the Shot
	// TrackInstance pushes its first override (TrackInstance::OnAnimate runs
	// in the later evaluation phase). With the override map still empty at
	// flip time, `ApplyActiveSequencerShotOverride` is a no-op,
	// CompositionFramingNode falls back to its default (empty Targets) Shot,
	// the solver returns invalid, and the upstream identity pose lands on
	// the CineCamera — visually "B's camera at world origin" for the cut
	// frame in PIE / Game. Editor scrub doesn't hit this because the gate
	// bypasses editor worlds entirely (see GateInstantiator) so B's
	// component has been ticking continuously and CineCam is already at the
	// correct pose by the time the cut fires.
	//
	// Re-running EvaluateOnce here, AFTER we've just installed the very
	// first override entry for a section, runs the framing-node solver with
	// the now-populated `SequencerShotOverrides` and re-projects to the
	// CineCam in the same frame the cut renders. The `bEvaluationEnabled`
	// guard avoids spawning the InternalCamera for a Shot input the gate
	// has chosen NOT to enable (the `OnAnimate` walk pushes overrides for
	// every in-range section regardless of gate state, but only enabled
	// components should pay the per-tick evaluation cost).
	//
	// Cost: one extra EvaluateOnce on each section's first in-range frame.
	// Steady-state in-range frames hit the `!bIsNewEntry` short-circuit and
	// rely on the normal TickComponent path, so this is not a per-frame
	// regression.
	if (bIsNewEntry && bEvaluationEnabled)
	{
		// `EvaluateOnce` would short-circuit through `TickCamera`'s
		// per-frame memoization if the gate's flip already ticked the
		// camera this frame (cached pose was solved against an empty
		// override map and is exactly the bad pose we're trying to
		// replace). LSComp's InternalCamera lives outside the snapshot
		// DAG, so bypassing the cache here is safe — see
		// `AComposableCameraCameraBase::InvalidateTickCache` doc.
		if (InternalCamera)
		{
			InternalCamera->InvalidateTickCache();
		}
		EvaluateOnce(0.f);
	}
}

void UComposableCameraLevelSequenceComponent::RemoveSequencerShotOverride(
	UMovieSceneComposableCameraShotSection* Section)
{
	if (!Section)
	{
		return;
	}
	SequencerShotOverrides.Remove(Section);
}

void UComposableCameraLevelSequenceComponent::ApplyActiveSequencerShotOverride()
{
	if (!InternalCamera)
	{
		return;
	}

	// ─── Phase F: collect valid entries sorted ascending by RowIndex.
	// Lowest RowIndex (Sequencer's "top row") = primary / outgoing.
	// Next-lowest RowIndex = secondary / incoming for the active overlap.
	// Third+ entries are silently dropped per spec §7.6 decision Q3.
	//
	// Stale entries (section GC'd) are pruned in the same pass to keep
	// the map tight.
	TArray<TWeakObjectPtr<UMovieSceneComposableCameraShotSection>> ToPrune;
	struct FSortedEntry
	{
		UMovieSceneComposableCameraShotSection*       Section;
		const FComposableCameraSequencerShotEntry*    Entry;
	};
	TArray<FSortedEntry, TInlineAllocator<4>> SortedEntries;
	for (const TPair<TWeakObjectPtr<UMovieSceneComposableCameraShotSection>, FComposableCameraSequencerShotEntry>& Pair : SequencerShotOverrides)
	{
		if (!Pair.Key.IsValid())
		{
			ToPrune.Add(Pair.Key);
			continue;
		}
		SortedEntries.Add({ Pair.Key.Get(), &Pair.Value });
	}
	for (const TWeakObjectPtr<UMovieSceneComposableCameraShotSection>& Stale : ToPrune)
	{
		SequencerShotOverrides.Remove(Stale);
	}

	if (SortedEntries.Num() == 0)
	{
		// No active overrides — clear the primary-section tracker so the
		// next override that arrives is treated as a fresh cut. Without
		// this, the gap-between-sections case would compare the new
		// section against the last one before the gap and (if they match)
		// fail to reseed even though the camera held its last pose
		// across the gap.
		LastActivePrimarySection = nullptr;
		return;
	}

	SortedEntries.Sort([](const FSortedEntry& A, const FSortedEntry& B)
	{
		return A.Entry->RowIndex < B.Entry->RowIndex;
	});

	const FComposableCameraSequencerShotEntry* Primary = SortedEntries[0].Entry;
	const FComposableCameraSequencerShotEntry* Secondary =
		SortedEntries.Num() >= 2 ? SortedEntries[1].Entry : nullptr;
	UMovieSceneComposableCameraShotSection* PrimarySection = SortedEntries[0].Section;

	// Find the first UComposableCameraCompositionFramingNode on the
	// InternalCamera's CameraNodes array. The DefaultShotTypeAsset bootstrap
	// (Phase E.5) guarantees exactly one CompositionFramingNode for the
	// AComposableCameraLevelSequenceShotActor flow; this "first found" rule
	// only matters when a power user pairs the generic LSActor with a custom
	// TypeAsset that contains multiple framing nodes (push to first; their
	// problem to architect around).
	UComposableCameraCompositionFramingNode* Framing = nullptr;
	for (UComposableCameraCameraNodeBase* Node : InternalCamera->CameraNodes)
	{
		if (UComposableCameraCompositionFramingNode* Cast0 = Cast<UComposableCameraCompositionFramingNode>(Node))
		{
			Framing = Cast0;
			break;
		}
	}

	if (!Framing)
	{
		UE_LOG(LogComposableCameraSystem, Verbose,
			TEXT("LS '%s': active Shot override but InternalCamera has no UComposableCameraCompositionFramingNode — silent skip."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));
		return;
	}

	// Push the effective render aspect to the framing node BEFORE pushing
	// shot data, so the next OnTickNode's solver sees the up-to-date value.
	// `GetEffectiveAspectRatioForCineCamera` resolves through:
	//   - CineCam->bConstrainAspectRatio == true  → CineCam->AspectRatio
	//     (filmback letterbox; renderer always crops to this regardless of
	//     viewport shape, solver must match for anchor screen positions to
	//     land where designers expect).
	//   - CineCam->bConstrainAspectRatio == false → live viewport aspect,
	//     resolved through `TryGetEffectiveViewportSize` (game viewport in
	//     PIE, editor active viewport during scrub via the new
	//     `FGetActiveEditorViewport` hook). Tracks the level viewport in
	//     real time so resizing the viewport repositions anchors correctly.
	const float EffectiveAspect =
		UE::ComposableCameras::GetEffectiveAspectRatioForCineCamera(OutputCineCameraComponent);
	Framing->SetExternalAspectRatioOverride(EffectiveAspect);

	// Detect a *primary section* transition (Section A → B with no overlap,
	// or section bind to a different ShotAsset). Drives V2.2 damping reseed
	// on the framing-node side so cuts are visually instantaneous instead
	// of glided. Comparison uses raw section pointers — TWeakObjectPtr
	// equality would re-validate every frame, the raw compare costs one
	// branch and is correct because the LastActivePrimarySection's weak
	// ptr was set from the same map-key TWeakObjectPtr last frame (no
	// dangling-after-GC risk here).
	const bool bPrimaryChanged = (LastActivePrimarySection.Get() != PrimarySection);
	LastActivePrimarySection = PrimarySection;

	// Push (Primary, Secondary?, Transition?, Alpha) tuple to the framing
	// node. The node's `SetActiveShotsFromSequencer` decides whether to run
	// the two-solver blend (F.4) based on whether secondary + transition
	// are both non-null. When `Secondary->EnterTransition` is null, the
	// node treats it as a hard cut — primary is the sole solver input,
	// matching V1 top-row-winner behavior throughout the overlap.
	if (Secondary)
	{
		Framing->SetActiveShotsFromSequencer(
			Primary->Shot,
			&Secondary->Shot,
			Secondary->EnterTransition,
			Secondary->BlendAlpha,
			bPrimaryChanged);
	}
	else
	{
		Framing->SetActiveShotsFromSequencer(
			Primary->Shot,
			/*InSecondaryShot=*/nullptr,
			/*InTransition=*/nullptr,
			/*InAlpha=*/0.0f,
			bPrimaryChanged);
	}
}

void UComposableCameraLevelSequenceComponent::BuildSequencerPatchSnapshot(
	TArray<FComposableCameraPatchSnapshot>& OutPatches) const
{
	if (SequencerPatchOverlays.Num() == 0)
	{
		return;
	}

	// Sort by section's effective LayerIndex — same order as ApplySequencerPatchOverlays
	// so panel rows reflect actual composition order. Matches PatchManager's
	// (LayerIndex asc) sort convention so the merged BP+Sequencer list reads
	// consistently regardless of which path produced each entry.
	TArray<UMovieSceneComposableCameraPatchSection*> SortedKeys;
	SortedKeys.Reserve(SequencerPatchOverlays.Num());
	for (const TPair<TObjectPtr<UMovieSceneComposableCameraPatchSection>, FComposableCameraSequencerPatchOverlay>& Pair : SequencerPatchOverlays)
	{
		if (Pair.Key && IsValid(Pair.Key))
		{
			SortedKeys.Add(Pair.Key);
		}
	}
	SortedKeys.Sort([](const UMovieSceneComposableCameraPatchSection& A,
	                   const UMovieSceneComposableCameraPatchSection& B)
	{
		auto LayerOf = [](const UMovieSceneComposableCameraPatchSection& S) -> int32
		{
			if (!S.PatchAsset) return 0;
			return S.Params.bOverrideLayerIndex ? S.Params.LayerIndex : S.PatchAsset->DefaultLayerIndex;
		};
		return LayerOf(A) < LayerOf(B);
	});

	const FString HostName = GetOwner() ? GetOwner()->GetName() : TEXT("<no owner>");

	for (UMovieSceneComposableCameraPatchSection* Section : SortedKeys)
	{
		const FComposableCameraSequencerPatchOverlay* Overlay = SequencerPatchOverlays.Find(Section);
		if (!Overlay)
		{
			continue;
		}

		FComposableCameraPatchSnapshot& Snap = OutPatches.AddDefaulted_GetRef();
		Snap.AssetName = Section->PatchAsset ? Section->PatchAsset->GetName() : TEXT("(missing)");
		Snap.LayerIndex = Section->PatchAsset
			? (Section->Params.bOverrideLayerIndex ? Section->Params.LayerIndex : Section->PatchAsset->DefaultLayerIndex)
			: 0;
		// Sequencer envelope is stateless — alpha alone carries phase info.
		// Encode phase as Active (1) so the panel doesn't try to render
		// "Entering" / "Exiting" timer rows that would always read zero.
		Snap.Phase = 1;
		Snap.Alpha = Overlay->Alpha;
		Snap.ElapsedInPhase = 0.f;
		Snap.ElapsedTimeActive = 0.f;
		// Convert section duration to seconds for the Duration field display.
		const TRange<FFrameNumber> Range = Section->GetTrueRange();
		float SectionSeconds = 0.f;
		if (Range.HasLowerBound() && Range.HasUpperBound())
		{
			if (const UMovieScene* OwnerScene = Section->GetTypedOuter<UMovieScene>())
			{
				const FFrameRate TickRate = OwnerScene->GetTickResolution();
				const int32 SizeTicks = (Range.GetUpperBoundValue() - Range.GetLowerBoundValue()).Value;
				SectionSeconds = TickRate.AsSeconds(FFrameTime(SizeTicks));
			}
		}
		Snap.Duration = SectionSeconds;
		// Per-Params overrides win, else asset defaults — same precedence the
		// runtime overlay path uses; the values the user actually sees in the
		// envelope ramp this frame.
		Snap.EnterDuration = Section->Params.bOverrideEnterDuration
			? Section->Params.EnterDuration
			: (Section->PatchAsset ? Section->PatchAsset->DefaultEnterDuration : 0.f);
		Snap.ExitDuration = Section->Params.bOverrideExitDuration
			? Section->Params.ExitDuration
			: (Section->PatchAsset ? Section->PatchAsset->DefaultExitDuration : 0.f);
		Snap.ExpirationType = 0;
		Snap.bExpireOnCameraChange = false;
		Snap.Source = EComposableCameraPatchSource::Sequencer;
		Snap.HostActorName = HostName;
	}
}
