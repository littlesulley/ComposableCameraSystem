// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LevelSequence/ComposableCameraTypeAssetReference.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraLevelSequenceComponent.generated.h"

class AComposableCameraCameraBase;
class UCineCameraComponent;
struct FComposableCameraPose;

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
 * Pure UActorComponent — NOT a USceneComponent. The component holds no
 * transform of its own; it is a logic-and-data driver. The owning Actor is
 * expected to provide a UCineCameraComponent as its RootComponent (that's
 * what AComposableCameraLevelSequenceActor does), and to hand us the
 * reference via OutputCineCameraComponent during construction. If a designer
 * adds this component to an arbitrary Actor (BlueprintSpawnableComponent),
 * OnRegister falls back to FindComponentByClass<UCineCameraComponent>(GetOwner())
 * — the component will then drive whatever CineCamera is first found on the
 * owning Actor, or be a no-op if none exists.
 *
 * Why ActorComponent not SceneComponent
 * ─────────────────────────────────────
 * Previously this was a USceneComponent whose RootComponent role doubled as
 * a parent for the CineCamera child. That arrangement collided with UE's
 * DefaultSubobject semantics (a component creating its own
 * CreateDefaultSubobject<UCineCameraComponent> registered the CineCamera as
 * a sub-subobject of the component, invisible to the Actor's component tree
 * and therefore invisible to USceneComponent::GetChildrenComponents and
 * AActor::FindComponentByClass<UCameraComponent>). PCM::SetViewTarget's
 * implicit-activation filter relies on that traversal and silently bailed,
 * which manifested as "second camera never activates" for blended Camera
 * Cut sections — see the diagnostic log that uncovered it.
 *
 * With the CineCamera as the Actor's RootComponent and this component as a
 * plain sibling UActorComponent, the engine's standard "find a CameraComponent
 * on the actor" path trivially finds the root, PCM::SetViewTarget creates
 * the proxy, and we go down the same fast path ACineCameraActor uses. No
 * special-case walks needed.
 *
 * Compatibility & responsibilities remain unchanged:
 *   - PCM-independent evaluation (via UE::ComposableCameras::ConstructCameraFromTypeAsset).
 *   - Per-tick bag → RuntimeDataBlock re-sync before TickCamera.
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
	 *  CineCamera — the Actor does. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Composable Camera")
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

	// ─── Sequencer-facing API ────────────────────────────────────────────

	/**
	 * Gate for on-demand ticking.
	 *
	 * Default: ON. OnRegister unconditionally calls SetEvaluationEnabled(true)
	 * so every LS Actor ticks by default (same as pre-Phase-G behavior). The
	 * ECS gate (UMovieSceneComposableCameraGateInstantiator) does not "open"
	 * the gate — it CLOSES it for tracked entities that aren't currently the
	 * Camera Cut Track's target or a blend participant. Entities it cannot
	 * reach (pre-upgrade LS assets, UE 5.5+ custom-binding spawnables the hook
	 * doesn't see, non-Sequencer hosts) keep the default always-on behavior,
	 * which is the correct graceful degradation.
	 *
	 * Toggling to false tears down the internal camera so the Actor can go
	 * fully idle; toggling back to true respawns it lazily on the first tick.
	 */
	void SetEvaluationEnabled(bool bEnabled);
	bool IsEvaluationEnabled() const { return bEvaluationEnabled; }

	/**
	 * Forward-compat hooks for a future ECS instantiator. The V1.4 simplified
	 * path doesn't route through these — Sequencer's stock property tracks
	 * write the bag directly and the per-tick ApplyParameterBlock picks it up.
	 * Left in place (as no-ops) so external integrations don't have to be
	 * re-wired when a proper instantiator is added later.
	 */
	void SetParameterValue(FName Name, const void* Value, EComposableCameraPinType Type);
	void SetVariableValue(FName Name, const void* Value, EComposableCameraPinType Type);

	/** Access the internal camera for editor-side inspection / debugging. */
	AComposableCameraCameraBase* GetInternalCamera() const { return InternalCamera; }

private:
	/** Transient internal camera — spawned lazily on first evaluation. Not
	 *  added to any context stack or director; driven entirely by this
	 *  component's TickComponent. */
	UPROPERTY(Transient)
	TObjectPtr<AComposableCameraCameraBase> InternalCamera;

	/** Gate for on-demand ticking; see SetEvaluationEnabled. */
	bool bEvaluationEnabled = false;

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
};
