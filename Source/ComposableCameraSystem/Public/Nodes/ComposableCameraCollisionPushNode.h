// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraCollisionPushNode.generated.h"

class UComposableCameraInterpolatorBase;

/**
 * Node for resolving collision using self-spherical collision and trace collision.\n
 * This node does two things for collision: \n
 * (1) Casts a trace (either line or sphere) from camera to the target point and resolves any occlusion in-between; \n
 * (2) Carries a sphere around the camera and only resolves occlusion when the sphere collides with objects. \n
 * The first we call it TraceCollision, and the second SelfCollision, both dealt with collision channels. \n
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCollisionPushNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	// Collision channel for trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TEnumAsByte<ECollisionChannel> TraceCollisionChannel;

	// Whether to use sphere for trace collision detection. If false, use line trace.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bTraceUseSphere { true };

	// If use sphere trace, the radius of the sphere.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bTraceUseSphere == true"))
	double TraceSphereRadius { 15. };

	// Occlusion must remain this amount of time to trigger collision resolution. For trace collision only.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double TraceOcclusionExemptionTime { 0. };;
	
	// Collision channel for self collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TEnumAsByte<ECollisionChannel> SelfCollisionChannel;
	
	// Radius of the self-collision sphere.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double SelfSphereRadius { 15. };

	// The distance between the self-collision sphere center and camera position along the camera looking direction.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double SelfSphereDistanceOffsetFromCenter { 10. };

	// Extra distance to push the camera forward after resolving collision. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double ExtraPushDistance { 10. };

	// Push interpolator when triggering collision resolution. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	UComposableCameraInterpolatorBase* PushInterpolator;

	// Pull interpolator when resuming from collision resolution. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	UComposableCameraInterpolatorBase* PullInterpolator;

	// Z offset to the ContextPivotActor, the final position will be the target collision detection point.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double PivotZOffset { 50. };

	// Whether to use a bone as the target collision detection point.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bUseBoneForDetection { false };

	// If use bone, specify the bone name. If we cannot find such a bone, will turn to use PivotZOffset.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == true"))
	FName BoneName;
	
	// The context actor from which we retrieve the target collision detection position.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ContextParameters)
	FActorComposableCameraContextParameter ContextPivotActor;;
};
