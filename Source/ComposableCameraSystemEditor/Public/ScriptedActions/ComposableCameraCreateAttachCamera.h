// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorActionUtility.h"
#include "ComposableCameraCreateAttachCamera.generated.h"

/**
 * Utility class for creating a camera actor, attaching it to the selected actor and piloting the camera actor.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraCreateAttachCamera: public UActorActionUtility
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor)
	void CreateAndAttachCamera();
};
