// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraActorInputSource.generated.h"

class AActor;
class AComposableCameraPlayerCameraManager;

/**
 * Common actor-source selector for nodes that usually operate on the local
 * controller's pawn but still need an explicit actor override.
 */
UENUM(BlueprintType)
enum class EComposableCameraActorInputSource : uint8
{
	ExplicitActor UMETA(DisplayName = "Explicit Actor"),
	ControllerControlledPawn UMETA(DisplayName = "Controller Controlled Pawn"),
};

namespace ComposableCameraSystem
{
	COMPOSABLECAMERASYSTEM_API AActor* ResolveActorInput(
		EComposableCameraActorInputSource Source,
		AActor* ExplicitActor,
		const AComposableCameraPlayerCameraManager* PlayerCameraManager);
}
