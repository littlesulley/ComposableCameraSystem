// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraActorInputSource.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	UWorld* ResolveWorldForActorInput(
		const AComposableCameraPlayerCameraManager* PlayerCameraManager,
		const UObject* WorldContextObject)
	{
		if (WorldContextObject)
		{
			if (UWorld* World = WorldContextObject->GetWorld())
			{
				return World;
			}

			if (const AActor* OuterActor = WorldContextObject->GetTypedOuter<AActor>())
			{
				return OuterActor->GetWorld();
			}
		}

		return PlayerCameraManager ? PlayerCameraManager->GetWorld() : nullptr;
	}

	const APlayerController* ResolveControllerForActorInput(
		const AComposableCameraPlayerCameraManager* PlayerCameraManager,
		const UObject* WorldContextObject)
	{
		if (PlayerCameraManager)
		{
			if (const APlayerController* PC = PlayerCameraManager->GetOwningPlayerController())
			{
				return PC;
			}
		}

		UWorld* World = ResolveWorldForActorInput(PlayerCameraManager, WorldContextObject);
		return World ? World->GetFirstPlayerController() : nullptr;
	}
}

namespace ComposableCameraSystem
{
	AActor* ResolveActorInput(
		EComposableCameraActorInputSource Source,
		AActor* ExplicitActor,
		const AComposableCameraPlayerCameraManager* PlayerCameraManager,
		const UObject* WorldContextObject)
	{
		if (Source == EComposableCameraActorInputSource::ControllerControlledPawn)
		{
			if (const APlayerController* PC = ResolveControllerForActorInput(PlayerCameraManager, WorldContextObject))
			{
				return PC->GetPawn();
			}
			return nullptr;
		}

		return ExplicitActor;
	}
}
