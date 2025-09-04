// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "Variables/ComposableCameraParameter.h"
#include "ComposableCameraCameraNodeBase.generated.h"

class AComposableCameraCameraBase;
class AComposableCameraPlayerCamaraManager;
class UComposableCameraPoseContextBase;
struct FComposableCameraPose;

/**
 * Base node for all camera nodes.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraNodeBase
	: public UObject
{
	GENERATED_BODY()

public:
	void Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCamaraManager* InPlayerCameraManager);
	void TickNode(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	void BeginPlayNode();

	TArray<TSubclassOf<UComposableCameraPoseContextBase>> GetRequiredContextClasses() const { return RequiredContextClasses; }
	
	void AddContextClass(TSubclassOf<UComposableCameraPoseContextBase> ContextClass, UComposableCameraPoseContextBase* Context)
	{
		ContextClassToContextMap.Add(ContextClass, Context);
	}
	
	void ClearContexts()
	{
		ContextClassToContextMap.Empty();
	}
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	FGameplayTag GetOwningCameraTag() const;

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	AComposableCameraCameraBase* GetOwningCamera() const { return OwningCamera; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	TArray<UComposableCameraPoseContextBase*> GetOwningCameraPoseContexts() const;

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node", meta = (DeterminesOutputType = "ContextClass"))
	UComposableCameraPoseContextBase* GetOwningCameraPoseContextByClass(TSubclassOf<UComposableCameraPoseContextBase> ContextClass) const;

protected:
	/**
	 * Do something when this node is initialized. This is useful for overriding default node parameters from running gameplay.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "InitializeNode", Category = "ComposableCameraSystem|Node")
	void OnInitialize();

	/**
	 * Main node logic implemented here. This node can read/write CameraPoseContext and/or CameraPose.
	 * You can use GetOwningCameraPoseContexts() to access all owning camera pose contexts or GetOwningCameraPoseContextByClass for a context of particular types.
	 * @param DeltaTime Delta time for this frame.
	 * @param CurrentCameraPose Current camera pose.
	 * @param OutCameraPose Output camera pose for this node.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "TickNode", Category = "ComposableCameraSystem|Node")
	void OnTickNode(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) {}

	/**
	 * Do something when this node starts to play. This is usually when you'd like to override the camera pose context, or cache contexts where you know their types.
	 * You can use GetOwningCameraPoseContexts() to access all owning camera pose contexts or GetOwningCameraPoseContextByClass for a context of particular types.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "BeginPlayNode", Category = "ComposableCameraSystem|Node")
	void OnBeginPlayNode();
	virtual void OnBeginPlayNode_Implementation() {}

public:
	/** Required context classes for this node class. These contexts will be generated during initialization. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<TSubclassOf<UComposableCameraPoseContextBase>> RequiredContextClasses;

protected:
	UPROPERTY(Transient)
	TMap<TSubclassOf<UComposableCameraPoseContextBase>, UComposableCameraPoseContextBase*> ContextClassToContextMap;
	
private:
	AComposableCameraCameraBase* OwningCamera;
	AComposableCameraPlayerCamaraManager* OwningPlayerCameraManager;
};
