// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraPivotDampingNode.generated.h"

/**
 * Node for damping (interpolating) the pivot position. This is done in the space projected onto the XY plane. \n
 * @InputParameter MaintainCameraSpacePivot: Whether to maintain camera space pivot position. \n
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
	// Whether to maintain camera space pivot position.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bMaintainCameraSpacePivotPosition { true };
	
	// Interpolator when the pivot is moving upward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* UpwardInterpolator;

	// Interpolator when the pivot is moving downward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* DownwardInterpolator;

	// Interpolator when the pivot is moving leftward. 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* LeftwardInterpolator;

	// Interpolator when the pivot is moving rightward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* RightwardInterpolator;

	// Interpolator when the pivot is moving forward. 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* ForwardInterpolator;

	// Interpolator when the pivot is moving backward. 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* BackwardInterpolator;

	// Pivot position to read. Damping is applied to this value too.
	UPROPERTY(EditAnywhere, Category = ContextParameters)
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
