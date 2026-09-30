// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraNamespaces.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "UObject/Object.h"
#include "ComposableCameraModifierRuntimeState.generated.h"

class AComposableCameraCameraBase;
class FProperty;
class UComposableCameraCameraNodeBase;
class UComposableCameraModifierBase;
class UComposableCameraModifierTransitionBase;
class UComposableCameraNodeModifierDataAsset;

enum class EComposableCameraModifierPropertyPhase : uint8
{
	Entering,
	Replacing,
	Active,
	Exiting
};

/** Owned, display-ready values. Built only when the runtime debug panel is visible. */
struct COMPOSABLECAMERASYSTEM_API FComposableCameraModifierPropertyDebugSnapshot
{
	FString NodeClassPath;
	FString NodeClassName;
	FString PropertyName;
	FString OwnerName;
	FString CurrentValue;
	FString TargetValue;
	EComposableCameraModifierPropertyPhase Phase =
		EComposableCameraModifierPropertyPhase::Active;
	float Progress = 1.f;
};

/** One cached property operation. Reflection and pin discovery happen only when this is built. */
USTRUCT()
struct FComposableCameraModifierPropertyState
{
	GENERATED_BODY()

	FName PropertyName;
	FProperty* Property = nullptr;
	int32 FieldOffset = INDEX_NONE;

	bool bHasInputPin = false;
	UPROPERTY(Transient)
	FComposableCameraNodePinDeclaration InputPin;
	bool bContinuous = false;
	bool bSourceUsesLiveLower = false;
	bool bDiscreteTargetApplied = false;
	bool bPendingRemoval = false;

	EComposableCameraModifierPropertyPhase Phase =
		EComposableCameraModifierPropertyPhase::Entering;
	float ElapsedTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraCameraNodeBase> SourceSnapshot;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraCameraNodeBase> TargetTemplate;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraModifierTransitionBase> Transition;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraModifierBase> DesiredModifier;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraNodeModifierDataAsset> DesiredAsset;
};

/** Per-runtime-node state. Multiple nodes of the same exact class remain independent. */
USTRUCT()
struct FComposableCameraModifierNodeState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraCameraNodeBase> Node;

	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraCameraNodeBase> BaselineSnapshot;

	UPROPERTY(Transient)
	TArray<FComposableCameraModifierPropertyState> Properties;
};

/**
 * Camera-owned state for ModifyExistingInstance assets.
 *
 * Selection stays in the PCM Modifier Manager. This object owns only
 * per-camera baselines, transition progress, and cached property bindings.
 */
UCLASS(Transient)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierRuntimeState : public UObject
{
	GENERATED_BODY()

public:
	void ApplyInitialEffectiveModifiers(
		AComposableCameraCameraBase* Camera,
		const ComposableCameraModifier::T_EffectiveModifier& EffectiveModifiers);

	/** Legacy one-entry-per-node compatibility overload. */
	void ApplyInitialModifiers(
		AComposableCameraCameraBase* Camera,
		const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers);

	void ReconcileEffectiveModifiersFromAssets(
		AComposableCameraCameraBase* Camera,
		const ComposableCameraModifier::T_EffectiveModifier& EffectiveModifiers);

	/** Legacy one-entry-per-node compatibility overload. */
	void ReconcileModifiersFromAssets(
		AComposableCameraCameraBase* Camera,
		const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers);

	void ReconcileModifiers(
		AComposableCameraCameraBase* Camera,
		const ComposableCameraModifier::T_NodeModifier& EffectiveModifiers,
		UComposableCameraModifierTransitionBase* TransitionOverride);

	void BeginCameraTick(float DeltaTime);
	void ApplyForNode(UComposableCameraCameraNodeBase* Node);
	void EndCameraTick();

	bool HasBindings() const;

	/** Read-only, on-demand view of live per-property bindings. */
	void BuildDebugSnapshot(
		TArray<FComposableCameraModifierPropertyDebugSnapshot>& Out) const;

private:
	UPROPERTY(Transient)
	TArray<FComposableCameraModifierNodeState> NodeStates;

	bool bSuppressNotifications = false;

	void ReconcileModifiersInternal(
		AComposableCameraCameraBase* Camera,
		const ComposableCameraModifier::T_EffectiveModifier& EffectiveModifiers,
		UComposableCameraModifierTransitionBase* TransitionOverride,
		bool bResolveTransitionsFromAssets,
		bool bImmediate);

	FComposableCameraModifierNodeState* FindNodeState(
		UComposableCameraCameraNodeBase* Node);

	void ReconcileNode(
		FComposableCameraModifierNodeState& NodeState,
		const ComposableCameraModifier::T_PropertyModifier* DesiredModifiers,
		UComposableCameraModifierTransitionBase* TransitionOverride,
		bool bResolveTransitionsFromAssets,
		bool bImmediate);

	FComposableCameraModifierPropertyState* AddPropertyBinding(
		FComposableCameraModifierNodeState& NodeState,
		FName PropertyName,
		UComposableCameraCameraNodeBase* TargetTemplate,
		UComposableCameraModifierTransitionBase* Transition,
		bool bImmediate);

	void StartTransitionToModifier(
		FComposableCameraModifierPropertyState& PropertyState,
		UComposableCameraCameraNodeBase* SourceSnapshot,
		UComposableCameraCameraNodeBase* TargetTemplate,
		UComposableCameraModifierTransitionBase* Transition,
		bool bUseLiveLowerSource,
		bool bImmediate);

	void StartExitTransition(
		FComposableCameraModifierPropertyState& PropertyState,
		UComposableCameraCameraNodeBase* SourceSnapshot,
		UComposableCameraModifierTransitionBase* Transition,
		bool bImmediate);

	void ApplyProperty(
		FComposableCameraModifierNodeState& NodeState,
		FComposableCameraModifierPropertyState& PropertyState);

	void ReleaseProperty(
		FComposableCameraModifierNodeState& NodeState,
		FComposableCameraModifierPropertyState& PropertyState);

	void PruneEmptyNodeStates();
};
