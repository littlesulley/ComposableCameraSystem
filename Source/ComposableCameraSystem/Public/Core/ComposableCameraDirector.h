// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraPlayerCameraManager.h"
#include "UObject/Object.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "ComposableCameraDirector.generated.h"

class AComposableCameraCameraBase;
class UComposableCameraEvaluationTree;
class UComposableCameraTransitionBase;
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraTransitionDataAsset;
class UComposableCameraPatchManager;

UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraDirector : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraDirector(const FObjectInitializer& ObjectInitializer);

	AComposableCameraCameraBase* ResumeCamera(
		AComposableCameraCameraBase* ResumeCamera,
		UComposableCameraTransitionBase* Transition,
		const FTransform& Transform);

	AComposableCameraCameraBase* CreateNewCamera(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		const FComposableCameraActivateParams& ActivationParams);

	AComposableCameraCameraBase* ActivateNewCamera(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionDataAsset* TransitionDataAsset,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent);

	/**
	 * Activate a new camera using a raw transition instance (not wrapped in a DataAsset).
	 * Used by ActivateNewCameraFromTypeAsset when the type asset provides its own
	 * DefaultTransition as an instanced UComposableCameraTransitionBase*.
	 * The transition is duplicated into the Director's context before use.
	 */
	AComposableCameraCameraBase* ActivateNewCamera(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionBase* TransitionInstance,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent);

	/**
	 * Activate a new camera with a reference to another Director as the transition source.
	 * Used for inter-context transitions: the reference leaf evaluates the source Director live.
	 */
	AComposableCameraCameraBase* ActivateNewCameraWithReferenceSource(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionDataAsset* TransitionDataAsset,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent,
		UComposableCameraDirector* SourceDirector,
		UComposableCameraTransitionBase** OutTransition = nullptr);

	/**
	 * Inter-context activation using a raw transition instance.
	 * Used by ActivateNewCameraFromTypeAsset when the type asset provides a DefaultTransition.
	 *
	 * `OutTransition` (optional): the duplicated transition instance installed in
	 * the tree's new Inner. Callers that need to bind cleanup to the activation's
	 * lifecycle (e.g. the context stack, which demotes non-top transient contexts
	 * to PendingDestroyEntries and clears them from this transition's
	 * `OnTransitionFinishesDelegate`) read it via this out-parameter.
	 */
	AComposableCameraCameraBase* ActivateNewCameraWithReferenceSource(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionBase* TransitionInstance,
		const FComposableCameraActivateParams& ActivationParams,
		FOnCameraFinishConstructed OnPreBeginplayEvent,
		UComposableCameraDirector* SourceDirector,
		UComposableCameraTransitionBase** OutTransition = nullptr);

	/**
	 * Resume this director's ALREADY-RUNNING camera with an inter-context
	 * transition whose source is `SourceDirector`'s output.
	 *
	 * Unlike `ActivateNewCameraWithReferenceSource`, this path does NOT
	 * spawn a new camera and does NOT destroy the current one. The
	 * existing `RunningCamera` stays in place and keeps all its per-node
	 * state (damping, interpolator, spline progress, etc.). The only
	 * mutation to the tree is wrapping its current `RootNode` as the
	 * right child of a new Inner node holding the pop transition + a
	 * `RefLeaf SourceDirector` as the left child.
	 *
	 * This is the correct code path for context-stack pops: the camera
	 * that was running before the push should resume with no state reset.
	 * Treat `ActivateNewCameraWithReferenceSource` as the "switch to a
	 * fresh instance" path (used for pushes / new activations) and this
	 * as the "preserve existing instance" path.
	 *
	 * @param PlayerCameraManager  Owning PCM, used for DeltaTime on transition init.
	 * @param TransitionInstance   Already-duplicated transition (caller owns the DuplicateObject).
	 * @param SourceDirector       The popped director to reference as the blend source.
	 * @param bFreezeSourceCamera  If true, the RefLeaf returns SourceDirector's
	 *                             cached LastEvaluatedPose every frame instead
	 *                             of re-evaluating. Use when the source context
	 *                             is about to be destroyed and its live evaluation
	 *                             would be wasted work.
	 * @return The resumed (unchanged) camera, or nullptr if the tree had no camera to resume.
	 */
	AComposableCameraCameraBase* ResumeCurrentCameraWithReferenceSource(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		UComposableCameraTransitionBase* TransitionInstance,
		UComposableCameraDirector* SourceDirector,
		bool bFreezeSourceCamera = false);

	AComposableCameraCameraBase* ReactivateCurrentCamera(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UComposableCameraTransitionBase* Transition,
		const FOnCameraFinishConstructed& OnPreBeginplayEvent);

	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);

	/** Get the currently running (target) camera in this Director's evaluation tree. */
	AComposableCameraCameraBase* GetRunningCamera() const { return RunningCamera; }

	/** Read-only access to the director's evaluation tree.
	 *  Intended for debug tooling (viewport debug transition walker,
	 *  snapshot builders, tests). Returns the raw pointer. Do not cache
	 *  it across activations, since the tree is torn down with the director. */
	UComposableCameraEvaluationTree* GetEvaluationTree() const { return EvaluationTree; }

	/** Access to this director's PatchManager. Owner of active CameraPatches.
	 *  Lifetime: created in the director ctor, destroyed with the director. Stage 1
	 *  has the manager wired through but its Apply pass is a no-op stub (see
	 *  UComposableCameraPatchManager doc comment for the staging plan). */
	UComposableCameraPatchManager* GetPatchManager() const { return PatchManager; }

	/** Get the last evaluated (blended) pose from this Director. */
	const FComposableCameraPose& GetLastEvaluatedPose() const { return LastEvaluatedPose; }

	/** Get the previous frame's evaluated (blended) pose from this Director. */
	const FComposableCameraPose& GetPreviousEvaluatedPose() const { return PreviousEvaluatedPose; }

	/** Destroy all cameras in this Director's evaluation tree. Called when a context is popped. */
	void DestroyAllCameras();

	/** Populate the context snapshot's director-owned fields: RunningCameraDisplay,
	 *  LastPose, and the flattened TreeNodes (via the evaluation tree). The
	 *  ContextName / active / base / pending-destroy flags are populated by the
	 *  caller (UComposableCameraContextStack::BuildDebugSnapshot) since only
	 *  the stack knows its own structure. */
	void BuildDebugSnapshot(FComposableCameraContextSnapshot& OutSnapshot) const;

	/** Walk UObject references inside the non-UPROPERTY cached poses
	 *  `LastEvaluatedPose` / `PreviousEvaluatedPose`. Both poses' embedded
	 *  `FPostProcessSettings` carries TObjectPtr references to materials /
	 *  textures / WeightedBlendables that would otherwise be GC-blind here
	 *  (the poses are not reflected fields on this UObject). */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	UPROPERTY(Transient)
	UComposableCameraEvaluationTree* EvaluationTree { nullptr };

	UPROPERTY(Transient)
	AComposableCameraCameraBase* RunningCamera { nullptr };

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraPatchManager> PatchManager;

	/** Cached blended pose from the last Evaluate() call. Represents the Director's actual output. */
	FComposableCameraPose LastEvaluatedPose;

	/** Previous frame's blended pose. Used for velocity estimation in transitions. */
	FComposableCameraPose PreviousEvaluatedPose;

	void ForceCameraPoses(AComposableCameraCameraBase* Camera, const FTransform& Transform);
};                                                                                                                     