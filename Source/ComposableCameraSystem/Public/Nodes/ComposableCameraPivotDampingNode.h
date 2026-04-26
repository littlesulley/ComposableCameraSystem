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
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Smoothly interpolates pivot position movement with directional damping."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotDampingNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPivotDampingNode() { PaletteCategory = TEXT("Pivot"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnFirstTickNode_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

protected:
	
public:
	// The pivot position to damp. Almost always driven by an upstream pivot-producing
	// node via wire (or a context parameter); kept as a UPROPERTY so the Details
	// panel renders a native FVector widget and an authored default is available
	// when unwired.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	FVector PivotPosition { FVector::ZeroVector };

	// Whether to maintain camera space pivot position.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bMaintainCameraSpacePivotPosition { true };
	
	// Interpolator when the pivot is moving upward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> UpwardInterpolator;

	// Interpolator when the pivot is moving downward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> DownwardInterpolator;

	// Interpolator when the pivot is moving leftward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> LeftwardInterpolator;

	// Interpolator when the pivot is moving rightward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> RightwardInterpolator;

	// Interpolator when the pivot is moving forward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> ForwardInterpolator;

	// Interpolator when the pivot is moving backward.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> BackwardInterpolator;

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> UpwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> DownwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> LeftwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> RightwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ForwardInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> BackwardInterpolator_T;

	FVector LastPivotPosition;
};
