// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ComposableCameraDynamicDeocclusionTransition.generated.h"

class UCurveFloat;

USTRUCT(BlueprintType)
struct FComposableCameraRayFeeler
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "-180", ClampMax = "180"))
	float Yaw { 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "-90", ClampMax = "90"))
	float Pitch { 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	float Length { 100.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	float Radius { 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FVector Offset { FVector::ZeroVector };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	TObjectPtr<UCurveFloat> StrengthCurve { nullptr };

public:
	FVector GetRayStartPosition(const FComposableCameraPose& Pose) const
	{
		return Pose.Position;
	}

	FVector GetRayEndPosition(const FComposableCameraPose& Pose) const
	{
		FRotator R { Pitch, Yaw, 0.f };
		FVector Ray = R.RotateVector(FVector::ForwardVector) * Length;
		Ray = Pose.Rotation.RotateVector(Ray);
		return GetRayStartPosition(Pose) + Ray;
	}
};

/**
 * A dynamically deocclusive transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraDynamicDeocclusionTransition : public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlay_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;
	virtual FComposableCameraPose OnEvaluate_Implementation(float DeltaTime, const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose) override;

	// Deocclusion wraps a DrivingTransition and layers dynamic feeler
	// offsets on top. The timing curve IS the driving transition's.
	// Falls back to linear when DrivingTransition is unset.
	virtual float GetBlendWeightAt(float NormalizedTime) const override;

#if !UE_BUILD_SHIPPING
	// Gated on `CCS.Debug.Viewport.Transitions.DynamicDeocclusion`.
	// Standard triplet in red accent, plus the feeler rays emanating from
	// the current blended pose. Essential for tuning feeler angles.
	virtual void DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// The driving transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Transition")
	UComposableCameraTransitionBase* DrivingTransition;
	
	// Feelers for dynamic occlusion detection.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	TArray<FComposableCameraRayFeeler> Feelers;

	// Collision channel for feeler trace.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transition")
	TEnumAsByte<ETraceTypeQuery> TraceChannel;
	
	// Actor types to ignore for feelers.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transition")
	TArray<TSoftClassPtr<AActor>> ActorTypesToIgnore;

	// Deocclusion speed.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transition")
	float DeocclusionSpeed { 1.f };

	// Should wait for such time to resume if no occlusion is detected.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transition")
	float ResumeWaitingTime { 0.2f };

	// When the transition process exceeds this percentage, will resume to the base pose ignoring any occlusion.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transition", meta = (ClampMin = "0", ClampMax = "1"))
	float DeadPercentage { 0.8f };

	// When the transition process exceeds this percentage, will resume to the base pose ignoring any occlusion.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transition", meta = (ClampMin = "0", ClampMax = "1"))
	float ResumeSpeed { 0.8f };

private:
	FVector PreviousOffset { FVector::ZeroVector };
	float ElapsedWaitingTime { 0.f };

	// Snapshot of actors-to-ignore taken at OnBeginPlay (resolved from
	// `ActorTypesToIgnore` via GetAllActorsOfClass). Stored as
	// TWeakObjectPtr. Across the multi-frame lifetime of a transition the
	// ignored actors can be destroyed / GC'd, and a raw cached `AActor*`
	// would dangle. The runtime trace API needs a `TArray<AActor*>`, so
	// `ResolvedActorsToIgnore` is rebuilt from the weak snapshot each
	// Evaluate (cheap. Typical N is a handful, no hot-path alloc since
	// the array reuses its capacity across frames).
	TArray<TWeakObjectPtr<AActor>> ActorsToIgnoreWeak;
	TArray<AActor*> ResolvedActorsToIgnore;

	EDrawDebugTrace::Type DrawDebugType;
};
