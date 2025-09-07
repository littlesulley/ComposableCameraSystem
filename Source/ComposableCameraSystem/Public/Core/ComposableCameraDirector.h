// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "ComposableCameraDirector.generated.h"

class AComposableCameraCameraBase;

UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraDirector : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraDirector(const FObjectInitializer& ObjectInitializer);

	AComposableCameraCameraBase* ActivateNewCamera(TSubclassOf<AComposableCameraCameraBase> CameraClass, UDataTable* NodeInitializerDataTable, FGameplayTagContainer NodeInitializerTags, bool bIsTransient, float LifeTime);

private:
	AComposableCameraCameraBase* RunningCamera;

};
