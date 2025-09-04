// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraActor.h"
#include "UObject/Object.h"
#include "ComposableCameraCameraBase.generated.h"

class UComposableCameraVariableCollection;
class UComposableCameraCameraNodeBase;
class AComposableCameraPlayerCamaraManager;

UCLASS(BlueprintType, Blueprintable, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class UComposableCameraPoseContextBase : public UObject
{
	GENERATED_BODY()
};

UCLASS(ClassGroup = ComposableCameraSystem, CollapseCategories)
class UComposableCameraPoseContextPivotOnly : public UComposableCameraPoseContextBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	AActor* PivotActor { nullptr };

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	FVector PivotPosition = { 0, 0, 0 };
};

USTRUCT(BlueprintType)
struct FComposableCameraPose
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	FVector Position { 0, 0, 0 };

	UPROPERTY(BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	FRotator Rotation { 0, 0, 0 };

	UPROPERTY(BlueprintReadWrite, Category = "ComposableCameraSystem|CameraPose")
	double FieldOfView { 75.f };
};

/**
 * Base camera class.
 */
UCLASS(Abstract, DefaultToInstanced, BlueprintType, Blueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API AComposableCameraCameraBase
	: public ACameraActor
{
	GENERATED_BODY()

public:
	AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ComposableCameraSystem|Camera")
	FGameplayTag CameraTag {};
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Camera")
	TArray<UComposableCameraCameraNodeBase*> CameraNodes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "ComposableCameraSystem|Camera")
	TObjectPtr<UComposableCameraVariableCollection> ContextVariables;

protected:
	void Initialize(AComposableCameraPlayerCamaraManager* Manager);
	void BeginPlayCamera();
	void TickCamera(float DeltaTime);
	void UpdateCamera(float DeltaTime);

	/**
	 * Do something when finishing initializing. This is called before all nodes begin to play.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnInitializedCamera", Category = "ComposableCameraSystem|Camera")
	void OnInitialized();

	/**
	 * A function used to execute custom logic when internal camera tick finishes. You can use GetContextByClass to get context instance by context class.
	 * @param OldCameraPose Old camera pose for last frame.
	 * @param NewCameraPose New camera pose calculated by nodes.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnTickedCamera", Category = "ComposableCameraSystem|Camera")
	void OnTicked(const FComposableCameraPose& OldCameraPose, const FComposableCameraPose& NewCameraPose);

	/**
	 * A function used to fully or partially override the current camera pose. You can write your own camera update logic here.
	 * You can use GetContextByClass to get context instance by context class.
	 * @param DeltaTime World ticked delta time for this frame.
	 * @param CurrentCameraPose Current camera pose.
	 * @param OutPose The camera pose actually used for final camera position, location, FOV and other parameters.
	 * @return If true, use the returned OutPose as the actual pose. If false, use the pose calculated by nodes.
	 */
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "OnUpdateCamera", Category = "ComposableCameraSystem|Camera")
	bool OnUpdateCamera(float DeltaTime, FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutPose);

public:
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	AComposableCameraPlayerCamaraManager* GetOwningPlayerCameraManager() { return CameraManager; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetCameraPose() const { return CameraPose; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	FComposableCameraPose GetLastFrameCameraPose() const { return LastFrameCameraPose; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	TArray<UComposableCameraPoseContextBase*> GetAllContexts() const;
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	TArray<TSubclassOf<UComposableCameraPoseContextBase>> GetAllContextClasses() const;

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera", meta = (DeterminesOutputType = "ContextClass"))
	UComposableCameraPoseContextBase* GetContextByClass(TSubclassOf<UComposableCameraPoseContextBase> ContextClass) const;

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera")
	bool HasContextClass(const TSubclassOf<UComposableCameraPoseContextBase>& ContextClass) const;

public:
	UPROPERTY(Transient)
	FComposableCameraPose CameraPose;

	UPROPERTY(Transient)
	FComposableCameraPose LastFrameCameraPose;

	UPROPERTY(Transient)
	TMap<TSubclassOf<UComposableCameraPoseContextBase>, UComposableCameraPoseContextBase*> ContextClassToContextMap;

private:
	void GenerateContextClassToContextMap(const TArray<TSubclassOf<UComposableCameraPoseContextBase>>& ContextClasses);
	void DistributeContextsToNodes();
	
	TObjectPtr<AComposableCameraPlayerCamaraManager> CameraManager;
	
};
