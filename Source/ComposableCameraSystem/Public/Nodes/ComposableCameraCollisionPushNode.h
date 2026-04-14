// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraCollisionPushNode.generated.h"

class UComposableCameraInterpolatorBase;

struct FComposableCameraHitResult
{
	bool bHasHit;
	FVector HitTargetLocation;
};

/**
 * Node for resolving collision using self-spherical collision and trace collision.\n
 * This node does two things for collision: \n
 * (1) Casts a trace (either line or sphere) from camera to the target point and resolves any occlusion in-between; \n
 * (2) Carries a sphere around the camera and only resolves occlusion when the sphere collides with objects. \n
 * The first we call it TraceCollision, and the second SelfCollision, both dealt with collision channels. \n
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Pushes the camera away from obstacles using trace-based collision detection."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraCollisionPushNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void OnPreTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:
	
public:
	// Collision channel for trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TEnumAsByte<ETraceTypeQuery> TraceCollisionChannel;

	// Whether to use sphere for trace collision detection. If false, use line trace.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bTraceUseSphere { true };

	// If use sphere trace, the radius of the sphere.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bTraceUseSphere == true"))
	double TraceSphereRadius { 12. };

	// Occlusion must remain this amount of time to trigger collision resolution. For trace collision only.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double TraceOcclusionExemptionTime { 0. };;
	
	// Collision channel for self collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TEnumAsByte<ETraceTypeQuery> SelfCollisionChannel;
	
	// Radius of the self-collision sphere.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double SelfSphereRadius { 12. };

	// The distance between the self-collision sphere center and camera position along the camera looking direction.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double SelfSphereDistanceOffsetFromCenter { 10. };

	// Actor types to ignore for collision detection. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TArray<TSoftClassPtr<AActor>> ActorTypesToIgnore;

	// Extra distance to push the camera forward after resolving collision. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	double ExtraPushDistance { 5. };

	// Push interpolator when triggering collision resolution. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	UComposableCameraInterpolatorBase* PushInterpolator;

	// Pull interpolator when resuming from collision resolution. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	UComposableCameraInterpolatorBase* PullInterpolator;

	// World space Z offset to the pivot actor's position, used as the collision detection origin.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == false"))
	double PivotZOffset { 50. };

	// Whether to use a bone as the target collision detection point.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bUseBoneForDetection { false };

	// If use bone, specify the bone name. If we cannot find such a bone, will turn to use PivotZOffset.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bUseBoneForDetection == true"))
	FName BoneName;

private:
	FComposableCameraHitResult FindCollisionPoint(double DeltaTime,  const FVector& Start, const FVector& End, const FRotator& CameraRotation);
	FVector StartResolveCollision(double DeltaTime, const FVector& TargetLocation, const FVector& CameraPosition);
	FVector ResumeFromCollision(double DeltaTime, const FVector& PivotPosition, const FVector& CameraPosition);
	
private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> PushInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> PullInterpolator_T;
	
	USkeletalMeshComponent* SkeletalMeshComponentForPivotActor { nullptr };
	double ElapsedExemptionTime { 0. };
	double CurrentDistanceFromCamera { 0. };

	FVector OriginalCameraPosition;
};
