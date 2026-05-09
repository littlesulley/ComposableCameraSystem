// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraActorInputSource.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace ComposableCameraSystem
{
	AActor* ResolveActorInput(
		EComposableCameraActorInputSource Source,
		AActor* ExplicitActor,
		const AComposableCameraPlayerCameraManager* PlayerCameraManager)
	{
		if (Source == EComposableCameraActorInputSource::ControllerControlledPawn)
		{
			if (const APlayerController* PC = PlayerCameraManager ? PlayerCameraManager->GetOwningPlayerController() : nullptr)
			{
				return PC->GetPawn();
			}
			return nullptr;
		}

		return ExplicitActor;
	}
}
