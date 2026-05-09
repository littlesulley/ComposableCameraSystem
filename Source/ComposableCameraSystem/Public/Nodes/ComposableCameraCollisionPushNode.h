// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraCollisionPushNode.generated.h"

class UComposableCameraInterpolatorBase;
class USkeletalMeshComponent;

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
	UComposableCameraCollisionPushNode() { PaletteCategory = TEXT("Collision & Occlusion"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void OnPreTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

protected:
	
public:
	// Selects whether the collision pivot actor is the controller's controlled
	// pawn or the explicitly supplied PivotActor.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	EComposableCameraActorInputSource PivotActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Actor from which collision detection originates (pivot). Typically driven at
	// runtime via a context parameter (e.g. the player pawn); kept as a UPROPERTY
	// so the Details panel renders a proper object picker and an authored default
	// is available when unwired.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "PivotActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> PivotActor;

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
	TObjectPtr<UComposableCameraInterpolatorBase> PushInterpolator;

	// Pull interpolator when resuming from collision resolution. For both self collision and trace collision.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> PullInterpolator;

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

	// Cached SkeletalMeshComponent on the currently-resolved PivotActor.
	// TWeakObjectPtr (not raw, not UPROPERTY): PivotActor is an input pin
	// and can be driven to a new actor every frame, and the SkelMesh
	// component on that actor can be destroyed / re-spawned independently
	// of this node â€?Tick / DrawNodeDebug must IsValid()-check before
	// deref. Resolution happens lazily in Tick when the active PivotActor
	// differs from `LastResolvedPivotActor`, avoiding a per-frame
	// `GetComponentByClass` walk in the common stable-actor case.
	TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponentForPivotActor;
	TWeakObjectPtr<AActor> LastResolvedPivotActor;

	// Snapshot of "actors of every class in `ActorTypesToIgnore`" taken at
	// OnInitialize (and, if needed, refreshed on demand by the public-facing
	// `RefreshActorsToIgnoreCache` if a designer scripts it). Stored as
	// TWeakObjectPtr so multi-frame collision-ignore lifetime survives
	// individual actor destruction without dangling. Per-tick rebuild of
	// `ResolvedActorsToIgnore` from the weak snapshot is one weak-ptr `Get()`
	// per entry â€?typical N is small. Earlier behavior called
	// `GetAllActorsOfClass` every Tick which scaled with the world's actor
	// count and was a real cost on actor-heavy levels.
	TArray<TWeakObjectPtr<AActor>> ActorsToIgnoreWeak;
	TArray<AActor*> ResolvedActorsToIgnore;

	double ElapsedExemptionTime { 0. };
	double CurrentDistanceFromCamera { 0. };

	FVector OriginalCameraPosition;

#if !UE_BUILD_SHIPPING
	/** Cache populated in FindCollisionPoint each frame so DrawNodeDebug can
	 *  repaint the trace + self-collision sphere without re-running the
	 *  physics queries. All fields reset to ZeroVector / false each tick. */
	mutable FVector LastTraceStart        { FVector::ZeroVector };
	mutable FVector LastTraceEnd          { FVector::ZeroVector };
	mutable FVector LastTraceHitLocation  { FVector::ZeroVector };
	mutable FVector LastSelfSphereCenter  { FVector::ZeroVector };
	mutable bool    bLastTraceBlocked     { false };
#endif
};
