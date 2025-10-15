// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraSplineNode.generated.h"

class UComposableCameraInterpolatorBase;
class ACameraRig_Rail;

UENUM()
enum class EComposableCameraSplineNodeSplineType : uint8
{
	// Use UE's built-in spline (Bézier curve).
	BuiltInSpline,

	// Use custom Bézier curve.
	Bezier,

	// Cubic Hermite curve.
	CubicHermite,

	// Use B-Spline.
	BasicSpline,

	// Non-uniform rational B-spline.
	NURBS
};

UENUM()
enum class EComposableCameraSplineNodeMoveMethod : uint8
{
	// Automatically move the camera on spline.
	Automatic,

	// Manually move the camera on spline, will choose the closest point on spline.
	ClosestPoint
};

/**
 * Node for placing the camera on a given spline.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraSplineNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

protected:
	virtual void ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer) override;

public:
	// Spline type. The typical use is BuiltInSpline, i.e., the Unreal's built-in Bezier curve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSplineNodeSplineType SplineType { EComposableCameraSplineNodeSplineType::BuiltInSpline };

	// For BuiltInSpline. The rail instance the camera should be placed on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "SplineType == EComposableCameraSplineNodeSplineType::BuiltInSpline", EditConditionHides))
	TObjectPtr<ACameraRig_Rail> Rail { nullptr };

	// Move method on spline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSplineNodeMoveMethod MoveMethod { EComposableCameraSplineNodeMoveMethod::ClosestPoint };

	// Actor for ClosestPoint move method, receiving a FVector.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "EComposableCameraSplineNodeMoveMethod::ClosestPoint", EditConditionHides))
	TObjectPtr<AActor> ClosestMoveMethodPivotActor { nullptr };
	
	// Move speed curve for Automatic move method.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "EComposableCameraSplineNodeMoveMethod::Automatic", EditConditionHides))
	TObjectPtr<UCurveFloat> AutomaticMoveCurve { nullptr };

	// Whether to loop for Automatic move method.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "EComposableCameraSplineNodeMoveMethod::Automatic", EditConditionHides))
	bool bLoop { false };

	// Interpolator for all move methods.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, Instanced)
	UComposableCameraInterpolatorBase* MoveInterpolator;
	
	// Move offset normalized in [-1,1], applied to all move methods.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "-1", ClampMax = "1"))
	float MoveOffset { 0.f };
	
	// Determines whether the orientation of the camera should be in the tangent direction of the spline. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bLockOrientationOnSpline;
};
