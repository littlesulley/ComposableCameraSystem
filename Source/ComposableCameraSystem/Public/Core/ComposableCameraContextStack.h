// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "UObject/Object.h"
#include "ComposableCameraContextStack.generated.h"

class UComposableCameraDirector;
class UComposableCameraTransitionBase;
class UComposableCameraTransitionDataAsset;
class AComposableCameraPlayerCameraManager;

/**
 * A single entry in the camera context stack.
 * Each context owns its own Director (and thus its own EvaluationTree),
 * representing an independent camera "mode" (e.g., gameplay, level sequence, UI).
 */
USTRUCT()
struct FComposableCameraContextEntry
{
	GENERATED_BODY()

	/** The Director that manages cameras and transitions within this context. */
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraDirector> Director { nullptr };

	/** The name that identifies this context (from project settings). */
	FName ContextName;

	/** Last evaluated pose from this context. Used for inter-context blending on pop. */
	FComposableCameraPose LastPose;
};

/**
 * Camera context stack. The macro-level orchestrator.
 *
 * Manages a LIFO stack of camera evaluation contexts, each owning its own Director and EvaluationTree.
 * This is the first tier in the two-tier camera architecture:
 *   - Tier 1 (this): Context stack for switching between camera "modes" (gameplay, UI, cinematic).
 *   - Tier 2: Evaluation tree within each context for transitions between cameras of the same mode.
 *
 * Contexts are identified by FName and defined in project settings.
 * The stack is strict LIFO: new contexts push on top, popping removes from top (or by name).
 *
 * Auto-pop: when the active context's running camera is transient and finishes,
 * the context is automatically popped. The camera itself drives lifecycle, not the context.
 *
 * Inter-context transitions use a reference leaf node in the evaluation tree:
 * the new context's tree gets a reference leaf that evaluates the previous context's Director live,
 * enabling smooth blending between contexts.
 *
 * Popped contexts with transitions enter a "pending destruction" state: their Director stays alive
 * and is evaluated through the reference leaf during the transition. Once the transition finishes,
 * the pending context's cameras are destroyed and the entry is removed.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraContextStack : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraContextStack(const FObjectInitializer& ObjectInitializer);

	/**
	 * Ensure a context with the given name exists on the stack.
	 * If already present, returns its Director.
	 * If not, pushes a new context on top (LIFO) and returns its Director.
	 *
	 * @param PlayerCameraManager The owning player camera manager.
	 * @param ContextName The name identifying which context to ensure (must be in project settings).
	 * @return The Director for the context. Returns nullptr if the name is not registered.
	 */
	UComposableCameraDirector* EnsureContext(AComposableCameraPlayerCameraManager* PlayerCameraManager, FName ContextName);

	/**
	 * Pop a specific context by name.
	 * If the context is the active (top) context and a transition is available, the pop is animated:
	 * the previous context resumes with a transition from the popped context's Director.
	 * If the context is not the active one, it is removed immediately.
	 * Cannot pop the base (bottom) context if it is the last one remaining.
	 *
	 * @param ContextName The name of the context to pop.
	 * @param PlayerCameraManager The owning player camera manager (needed for camera creation during transition).
	 * @param TransitionOverride Optional transition data asset. If nullptr, falls back to the resume camera's EnterTransition.
	 * @param ActivationParams Optional activation params for the resume camera.
	 */
	void PopContext(
		FName ContextName,
		AComposableCameraPlayerCameraManager* PlayerCameraManager = nullptr,
		UComposableCameraTransitionDataAsset* TransitionOverride = nullptr,
		const FComposableCameraActivateParams& ActivationParams = FComposableCameraActivateParams());

	/**
	 * Pop the active (top) context. Used by TerminateCurrentCamera.
	 * Cannot pop the base context if it is the last one remaining.
	 *
	 * @param PlayerCameraManager The owning player camera manager.
	 * @param TransitionOverride Optional transition data asset override.
	 * @param ActivationParams Optional activation params for the resume camera.
	 */
	void PopActiveContext(
		AComposableCameraPlayerCameraManager* PlayerCameraManager = nullptr,
		UComposableCameraTransitionDataAsset* TransitionOverride = nullptr,
		const FComposableCameraActivateParams& ActivationParams = FComposableCameraActivateParams());

	/** Get the number of contexts on the stack. */
	int32 GetStackDepth() const { return Entries.Num(); }

	/** Get the active (top) context's Director. Returns nullptr if the stack is empty. */
	UComposableCameraDirector* GetActiveDirector() const;

	/** Get the Director for a specific context by name. Returns nullptr if not on the stack. */
	UComposableCameraDirector* GetDirectorForContext(FName ContextName) const;

	/** Get the active context's running camera. Returns nullptr if the stack is empty. */
	AComposableCameraCameraBase* GetRunningCamera() const;

	/** Get the active context's name. Returns NAME_None if the stack is empty. */
	FName GetActiveContextName() const;

	/**
	 * Evaluate the active context for this frame.
	 * Only the top context is ticked (unless a lower context is referenced by a reference leaf
	 * in the active context's tree, in which case it ticks through the reference).
	 * If the active context's running camera is transient and finished, auto-pops the context.
	 *
	 * @return The final camera pose for this frame.
	 */
	[[nodiscard]] FComposableCameraPose Evaluate(float DeltaTime);

	/**
	 * After an inter-context activation, demote every non-top entry whose
	 * running camera is transient into `PendingDestroyEntries`, treating each
	 * one as an implicit pop whose cleanup is bound to `ActivatingTransition`.
	 *
	 * Motivating scenario (the reason this exists):
	 *   1. Gameplay runs camera A.
	 *   2. Push Transient context with transient camera B (LifeTime 4s,
	 *      transition 5s).
	 *   3. Mid-push-transition, the caller invokes ActivateCamera(A, Gameplay)
	 *      to flip back to Gameplay. `EnsureContext(Gameplay)` rearranges the
	 *      stack to `[Transient, Gameplay]`; Transient drops off the top.
	 *   4. With only-top-scans-for-auto-pop semantics, Transient is now
	 *      permanently stuck on the stack: its camera B is no longer at the
	 *      top so the auto-pop loop in `Evaluate` never inspects it, and its
	 *      LifeTime expiring does not trigger any cleanup.
	 *
	 * Fix: at the end of the inter-context activation that caused the
	 * demotion, call this method with the activation's blend transition.
	 * Each non-top entry with a transient `RunningCamera` is removed from
	 * `Entries`, inserted into `PendingDestroyEntries`, and a lambda is
	 * registered on `ActivatingTransition->OnTransitionFinishesDelegate` so
	 * its director is destroyed + the pending entry removed exactly when the
	 * blend resolves. Matches the semantics of an explicit `PopContext` for
	 * that context.
	 *
	 * Why the delegate (instead of immediate destruction): the activation's
	 * new tree captures the Transient director's tree root as a RefLeaf
	 * snapshot. Destroying the transient camera immediately would leave the
	 * RefLeaf chain walking into `PendingKill` leaves during the blend. The
	 * same class of bug the `PendingDestroyOldRoots` defer fix addresses on
	 * the target-director side. The blend finish is the earliest moment the
	 * transient camera is reachable from no live tree.
	 *
	 * Non-transient entries that happen to be below the top are left alone;
	 * they are the caller's responsibility (e.g. a UI context temporarily
	 * suspended behind gameplay stays suspended).
	 *
	 * No-op if `ActivatingTransition` is null (camera-cut inter-context
	 * activation); in that case nothing is blending so any transient entry
	 * pushed below the top is destroyed immediately to match the single-
	 * frame semantics of a cut.
	 */
	void DemoteNonTopTransientContextsToPending(UComposableCameraTransitionBase* ActivatingTransition);

	/** Build a structured snapshot of the stack + each director's tree. The
	 *  snapshot is the single source of truth for every debug consumer: the
	 *  in-viewport 2D panel, the `showdebug camera` HUD, and any future dump
	 *  command all format their text by walking this snapshot through
	 *  `ComposableCameraDebug::AppendTreeNodeLine`. Contexts are emitted
	 *  top->base (index 0 of OutSnapshot.Contexts = active/top) followed by
	 *  all PendingDestroyEntries flagged bIsPendingDestroy = true. */
	void BuildDebugSnapshot(FComposableCameraContextStackSnapshot& OutSnapshot) const;

	// UObject interface.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	/** Stack entries in LIFO order (index 0 = base, Last() = active/top). */
	UPROPERTY(Transient)
	TArray<FComposableCameraContextEntry> Entries;

	/** Contexts that have been popped but are kept alive during their pop transition.
	 * Their Directors are still evaluated via reference leaves in the resume context's tree.
	 * Cleaned up when the transition finishes. */
	UPROPERTY(Transient)
	TArray<FComposableCameraContextEntry> PendingDestroyEntries;

	/** Find the index of a context by name. Returns INDEX_NONE if not found. */
	int32 FindContextIndex(FName ContextName) const;

	/**
	 * Internal: execute a pop of the active context with optional transition.
	 * Handles the transition setup, pending destruction, and cleanup.
	 */
	void PopActiveContextInternal(
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		UComposableCameraTransitionDataAsset* TransitionOverride,
		const FComposableCameraActivateParams& ActivationParams);
};
