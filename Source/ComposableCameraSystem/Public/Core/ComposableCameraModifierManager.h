// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraNamespaces.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "ComposableCameraModifierManager.generated.h"

class AComposableCameraCameraBase;
class UComposableCameraCameraNodeBase;
class UComposableCameraModifierBase;
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraTransitionBase;
class UComposableCameraModifierTransitionBase;
class AComposableCameraPlayerCameraManager;

using namespace ComposableCameraModifier;

/** Result of one effective-Modifier selection pass for the current camera. */
struct FComposableCameraModifierUpdateResult
{
	bool bChanged = false;
	bool bRequiresCameraReactivation = false;
	/** First legacy edge that requested reactivation; diagnostic only. */
	FString ReactivationReason;
	UComposableCameraTransitionBase* CameraTransition = nullptr;
	/**
	 * Compatibility summary for callers that still force one value transition
	 * over an entire update. PCM production evaluation derives property-local
	 * Enter/Replace/Exit transitions directly from the effective assets.
	 */
	UComposableCameraModifierTransitionBase* ModifierTransition = nullptr;
};

/**
 * An actor managing all camera modifiers.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierManager : public UObject
{
	GENERATED_BODY()

public:
	// FModifierEntry holds TObjectPtr references inside a non-reflected
	// nested TMap. Without this override the GC would not see those references -
	// callers that pass a transiently-rooted asset to AddModifier would see it
	// collected and the next UpdateEffectiveModifiers / ApplyModifiers would
	// dereference a dangling pointer.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	void AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset);
	void RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset);
	
public:
	struct FComposableCameraModifierData
	{
		// All registered candidates grouped by exact target node class. Each
		// candidate's CameraTagQuery is evaluated when the active camera changes.
		T_NodeModifierArray ModifierData;

		// Effective modifiers used by the current camera. Generic Node Type
		// entries compete per property. NAME_None stores the legacy whole-node
		// Custom Modifier winner when that branch wins the node-class bucket.
		T_EffectiveModifier EffectiveModifiers;

	public:
		// Update EffectiveModifiers and classify the change as legacy
		// reactivation or opt-in same-instance value mutation.
		FComposableCameraModifierUpdateResult UpdateEffectiveModifiers(
			AComposableCameraCameraBase* Camera);

		// Get current effective modifiers.
		T_EffectiveModifier& GetEffectiveModifiers()
		{
			return EffectiveModifiers;
		}
	};

	FComposableCameraModifierData& GetModifierData() { return ModifierData; }

	/** Const overload for read-only access (debug tooling / inspectors).
	 *  Returns the same struct by const reference - callers can iterate
	 *  the ModifierData / EffectiveModifiers maps but cannot mutate them. */
	const FComposableCameraModifierData& GetModifierData() const { return ModifierData; }

private:
	FComposableCameraModifierData ModifierData;
	uint64 NextRegistrationOrder = 1;
};
