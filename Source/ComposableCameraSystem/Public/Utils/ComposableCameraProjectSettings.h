// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ComposableCameraProjectSettings.generated.h"

/**
 * Developer settings for composable camera system.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Composable Camera System"))
class COMPOSABLECAMERASYSTEM_API UComposableCameraProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Depth for cleaning up camera chains when activating a new camera. \n
	// For example, current camera chain is C0 -> C1 -> C2 -> C3 -> C4, -> means the order of activation, C_i is the parent of C_{i+1}. \n
	// If this value is 5, and C5 is now activating, C0 will be erased from current chain, so C1 will have no parent camera, and the chain will be C1 -> ... -> C5. \n
	// This is useful for UObject garbage collection.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly)
	int32 MaxCameraChainCleanupDepth { 3 };
};
