// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "Variables/ComposableCameraParameter.h"
#include "ComposableCameraCameraNodeBase.generated.h"

class AComposableCameraCameraBase;
class AComposableCameraPlayerCamaraManager;
struct FComposableCameraPose;

/**
 * Base node for all camera nodes.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, Blueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraNodeBase
	: public UObject
{
	GENERATED_BODY()

public:
	void Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCamaraManager* InPlayerCameraManager);
	void TickNode(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	void BeginPlayNode(const FComposableCameraPose& CurrentCameraPose);
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	FGameplayTag GetOwningCameraTag() const;

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	AComposableCameraCameraBase* GetOwningCamera() const { return OwningCamera; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	AComposableCameraPlayerCamaraManager* GetOwningPlayerCameraManager() const { return OwningPlayerCameraManager; }
	
protected:
	/**
	 * Do something when this node is initialized. This is useful for overriding default node parameters from running gameplay.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "InitializeNode", Category = "ComposableCameraSystem|Node")
	void OnInitialize();

	/**
	 * Main node logic implemented here. This node can read/write ContextParameters and/or CameraPose.
	 * @param DeltaTime Delta time for this frame.
	 * @param CurrentCameraPose Current camera pose.
	 * @param OutCameraPose Output camera pose for this node.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "TickNode", Category = "ComposableCameraSystem|Node")
	void OnTickNode(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) {}

	/**
	 * Do something when this node starts to play. This is usually when you'd like to override the camera pose context, or initialize context parameters. \n
	 * @param CurrentCameraPose Current camera pose.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "BeginPlayNode", Category = "ComposableCameraSystem|Node")
	void OnBeginPlayNode(const FComposableCameraPose& CurrentCameraPose);
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) {}

protected:
	UPROPERTY(BlueprintReadOnly, Transient, Category = "ComposableCameraSystem|Node")
	AComposableCameraCameraBase* OwningCamera;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "ComposableCameraSystem|Node")
	AComposableCameraPlayerCamaraManager* OwningPlayerCameraManager;
};
