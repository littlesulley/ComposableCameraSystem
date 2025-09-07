// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "ComposableCameraBlueprintLibrary.generated.h"

class AComposableCameraCameraBase;

/**
 * Blueprint library.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Activate a composable camera by camera class, all derived from ComposableCameraCameraBase. \n
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param CameraClass The camera class to instantiate. \n
	 * @param NodeInitializerDataTable Data table for node initializers. If not set, no initializer will be applied. \n
	 * @param NodeInitializerTags Tags to use for node initializers. Only matched tags will be used. \n
	 * @param bNewInstance When the current running camera has the same camera class as CameraClass specified here, whether to instantiate a new camera. \n
	 * @param bIsTransient Whether this camera is transient. \n
	 * @param LifeTime The life time if this camera is transient. \n
	 * @return The instanced camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "CameraClass", AdvancedDisplay = 6, DataTablePin = "NodeInitDataTable"))
	static AComposableCameraCameraBase* ActivateComposableCameraByClass(const UObject* WorldContextObject, AComposableCameraPlayerCamaraManager* PlayerCameraManager, TSubclassOf<AComposableCameraCameraBase> CameraClass, UPARAM(meta = (RequiredAssetDataTags = "RowStructure=/Script/ComposableCameraSystem.ComposableCameraNodeInitializerTableRow")) UDataTable* NodeInitializerDataTable, FGameplayTagContainer NodeInitializerTags, bool bNewInstance = false, bool bIsTransient = false, float LifeTime = 0.f);
};
