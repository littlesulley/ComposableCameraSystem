// Copyright Sulley. All rights reserved.

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
class AComposableCameraPlayerCamaraManager;

using namespace ComposableCameraModifier;

/**
 * An actor managing all camera modifiers.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierManager : public UObject
{
	GENERATED_BODY()

public:
	void AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset);
	void RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset);
	
public:
	struct FComposableCameraModifierData
	{
		// All modifiers.
		T_CameraModifier ModifierData;

		// Effective modifiers that are used by current camera. For each node type, the modifier with the highest priority is tracked.
		T_NodeModifier EffectiveModifiers;

	public:
		// Update EffectiveModifiers for current camera and return if any modifier is changed in the list and the transition for new camera instance.
		std::pair<bool, UComposableCameraTransitionBase*> UpdateEffectiveModifiers(AComposableCameraCameraBase* Camera);

		// Get current effective modifiers.
		T_NodeModifier& GetEffectiveModifiers()
		{
			return EffectiveModifiers;
		}
	};

	FComposableCameraModifierData& GetModifierData() { return ModifierData; }

private:
	FComposableCameraModifierData ModifierData;
};
