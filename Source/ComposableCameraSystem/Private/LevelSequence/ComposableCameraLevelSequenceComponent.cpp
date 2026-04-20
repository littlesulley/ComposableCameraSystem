// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "CineCameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Engine/World.h"

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

	// Note: DeltaTime in editor-world scrubbing is synthetic (Sequencer drives
	// it) — history-dependent nodes may look off during scrub. Phase B accepts
	// this; see TechDoc §7 gotcha for history-dependent nodes. PIE DeltaTime is
	// real frame-to-frame time and nodes behave normally.
	const FComposableCameraPose Pose = InternalCamera->TickCamera(DeltaTime);
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
	if (bEvaluationEnabled != bEnabled)
	{
		// Diagnostic: log every gate transition with the owning actor's name
		// so you can follow which Spawnables the ECS gate is actually closing.
		// Enable with `Log LogComposableCameraSystem Log` (default) — these
		// fire only on change, so they're low-noise.
		UE_LOG(LogComposableCameraSystem, Log,
			TEXT("LS gate [%s]: %s → %s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"),
			bEvaluationEnabled ? TEXT("ON") : TEXT("OFF"),
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

void UComposableCameraLevelSequenceComponent::DestroyInternalCamera()
{
	if (InternalCamera && IsValid(InternalCamera))
	{
		InternalCamera->Destroy();
	}
	InternalCamera = nullptr;
}

void UComposableCameraLevelSequenceComponent::ProjectPoseToCineCamera(const FComposableCameraPose& Pose)
{
	if (!OutputCineCameraComponent)
	{
		return;
	}

	// Position and rotation only.
	//
	// The LS path deliberately does NOT project FOV / FocalLength / Filmback /
	// Aperture / Focus / PostProcess from the pose onto the CineCamera — those
	// belong to the CineCamera directly, where designers set them in Details
	// and Sequencer keys them via stock property tracks. The CCS TypeAsset
	// should not be declaring exposed parameters for physical optics; if a
	// TypeAsset DOES have a node that writes those pose fields, its output
	// is silently ignored in the LS path. This is the explicit design point
	// from the Phase B feedback: "FocalLength 这种参数应该不会作为
	// CameraTypeAsset 里 Exposed 的参数…让用户自己通过 OutputCineCameraComponent
	// 完全自己控制".
	//
	// Transform-only projection also keeps the LS path clean of the pose's
	// sentinel semantics (dual-mode FOV, PhysicalCameraBlendWeight, etc.) —
	// not a problem at this scope, but would be a mental-overhead tax to
	// reason about alongside Sequencer-keyed optics.
	OutputCineCameraComponent->SetWorldLocationAndRotation(Pose.Position, Pose.Rotation);
}
