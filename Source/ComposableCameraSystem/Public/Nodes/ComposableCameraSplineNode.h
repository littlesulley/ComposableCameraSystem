// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Math/ComposableCameraSplineInterface.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraSplineNode.generated.h"

class IComposableCameraSplineInterface;
class UComposableCameraInterpolatorBase;
class ACameraRig_Rail;

UENUM()
enum class EComposableCameraSplineNodeSplineType : uint8
{
	// Use UE's built-in spline (Bzier curve).
	BuiltInSpline,

	// Use custom Bzier curve.
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
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Positions the camera along a spline path with automatic or manual movement."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraSplineNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraSplineNode() { PaletteCategory = TEXT("Position"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

protected:

public:
	// Spline type. The typical use is BuiltInSpline, i.e., the Unreal's built-in Bzier curve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSplineNodeSplineType SplineType { EComposableCameraSplineNodeSplineType::BuiltInSpline };

	// For BuiltInSpline. The rail instance the camera should be placed on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "SplineType == EComposableCameraSplineNodeSplineType::BuiltInSpline", EditConditionHides))
	TObjectPtr<ACameraRig_Rail> Rail { nullptr };

	// Move method on spline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSplineNodeMoveMethod MoveMethod { EComposableCameraSplineNodeMoveMethod::ClosestPoint };

	// Selects whether ClosestPoint tracks the controller's controlled pawn or
	// the explicitly supplied ClosestMoveMethodPivotActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "MoveMethod == EComposableCameraSplineNodeMoveMethod::ClosestPoint", EditConditionHides))
	EComposableCameraActorInputSource ClosestMoveMethodPivotActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Actor for ClosestPoint move method, receiving a FVector.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "MoveMethod == EComposableCameraSplineNodeMoveMethod::ClosestPoint && ClosestMoveMethodPivotActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> ClosestMoveMethodPivotActor { nullptr };
	
	// Move curve for Automatic move method. X axis is normalized time in [0,1], Y axis is the normalized distance along the spline within [0,1].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "MoveMethod == EComposableCameraSplineNodeMoveMethod::Automatic", EditConditionHides))
	TObjectPtr<UCurveFloat> AutomaticMoveCurve { nullptr };
	
	// Duration for Automatic move method. If loop, this will be the time for traversing one round.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "MoveMethod == EComposableCameraSplineNodeMoveMethod::Automatic", EditConditionHides))
	float Duration { 3.0f };
	
	// Whether to loop for Automatic move method.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "MoveMethod == EComposableCameraSplineNodeMoveMethod::Automatic", EditConditionHides))
	bool bLoop { false };

	// Interpolator for all move methods.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, Instanced)
	TObjectPtr<UComposableCameraInterpolatorBase> MoveInterpolator;
	
	// Move offset normalized in [-1,1], applied to all move methods.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "-1", ClampMax = "1"))
	float MoveOffset { 0.f };
	
	// Determines whether the orientation of the camera should be in the tangent direction of the spline. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bLockOrientationOnSpline { false };

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> MoveInterpolator_T;
	IComposableCameraSplineInterface* SplineInterface;

	float ElapsedTimeForAutomaticMethod { 0.0f };
	bool bFirstLapIfLoop { true };

private:
	void UpdateCameraPoseByBuiltInSpline(FVector& OutPosition, FRotator& OutRotation, const FComposableCameraPose& CurrentCameraPose, float DeltaTime);
	void UpdateCameraPoseByBezierSpline(FVector& OutPosition, FRotator& OutRotation, const FComposableCameraPose& CurrentCameraPose, float DeltaTime);
	void UpdateCameraPoseByHermiteSpline(FVector& OutPosition, FRotator& OutRotation, const FComposableCameraPose& CurrentCameraPose, float DeltaTime);
	void UpdateCameraPoseByBasicSpline(FVector& OutPosition, FRotator& OutRotation, const FComposableCameraPose& CurrentCameraPose, float DeltaTime);
	void UpdateCameraPoseByNURBSpline(FVector& OutPosition, FRotator& OutRotation, const FComposableCameraPose& CurrentCameraPose, float DeltaTime);

};
