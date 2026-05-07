// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraActionBase.h"
#include "ComposableCameraMoveToAction.generated.h"

/**
 * This action moves the camera to a given position, regardless of camera rotation.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMoveToAction : public UComposableCameraActionBase
{
	GENERATED_BODY()

public:
	UComposableCameraMoveToAction(const FObjectInitializer& ObjectInitializer);

	virtual bool CanExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

	// Target position the camera moves to.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Action")
	FVector TargetPosition;

	// Move speed.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Action")
	float MoveSpeed { 1.f };
};
