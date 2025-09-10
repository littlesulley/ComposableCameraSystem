// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraPivotDampingNode.generated.h"

/**
 * Node for damping (interpolating) the pivot position. This is done in the space projected onto the XY plane. \n
 * @InputParameter UpwardInterpolator: Interpolator when the pivot is moving upward. \n
 * @InputParameter DownwardInterpolator: Interpolator when the pivot is moving downward. \n
 * @InputParameter LeftwardInterpolator: Interpolator when the pivot is moving leftward. \n
 * @InputParameter RightwardInterpolator: Interpolator when the pivot is moving rightward. \n
 * @InputParameter ForwardInterpolator: Interpolator when the pivot is moving forward. \n
 * @InputParameter BackwardInterpolator: Interpolator when the pivot is moving backward. \n
 * @ContextParameter ContextPivotPosition: Pivot position to read. Damping is applied to this value too.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotDampingNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* UpwardInterpolator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* DownwardInterpolator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* LeftwardInterpolator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* RightwardInterpolator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* ForwardInterpolator;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* BackwardInterpolator;

	UPROPERTY(EditDefaultsOnly, Category = ContextParameters)
	FVector3dComposableCameraContextParameter ContextPivotPosition;

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> UpwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> DownwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> LeftwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> RightwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ForwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> BackwardInterpolator_T;

	FVector LastPivotPosition;
};
