// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraBlueprintLibrary.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Kismet/GameplayStatics.h"

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UDataTable* NodeInitializerDataTable,
	FGameplayTagContainer NodeInitializerTags,
	bool bNewInstance,
	bool bIsTransient,
	float LifeTime)
{
	if (PlayerCameraManager)
	{
		if (PlayerCameraManager->GetRunningCamera()->StaticClass() == CameraClass->StaticClass() &&
			!PlayerCameraManager->GetRunningCamera()->IsTransient() &&
			!bIsTransient &&
			!bNewInstance)
		{
			return PlayerCameraManager->GetRunningCamera();
		}

		return PlayerCameraManager->ActivateNewCamera(CameraClass, NodeInitializerDataTable, NodeInitializerTags, bIsTransient, LifeTime);
	}

	return nullptr;
}
