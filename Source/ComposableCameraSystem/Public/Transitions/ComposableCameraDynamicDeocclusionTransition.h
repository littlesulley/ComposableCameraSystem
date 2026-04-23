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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-180", ClampMax = "180"))
	float Yaw { 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-90", ClampMax = "90"))
	float Pitch { 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Length { 100.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Radius { 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Offset { FVector::ZeroVector };

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
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
	// offsets on top — the timing curve IS the driving transition's.
	// Falls back to linear when DrivingTransition is unset.
	virtual float GetBlendWeightAt(float NormalizedTime) const override;

#if !UE_BUILD_SHIPPING
	// Gated on `CCS.Debug.Viewport.Transitions.DynamicDeocclusion`.
	// Standard triplet in red accent, plus the feeler rays emanating from
	// the current blended pose — essential for tuning feeler angles.
	virtual void DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// The driving transition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	UComposableCameraTransitionBase* DrivingTransition;
	
	// Feelers for dynamic occlusion detection.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FComposableCameraRayFeeler> Feelers;

	// Collision channel for feeler trace.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TEnumAsByte<ETraceTypeQuery> TraceChannel;
	
	// Actor types to ignore for feelers.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<TSoftClassPtr<AActor>> ActorTypesToIgnore;

	// Deocclusion speed.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float DeocclusionSpeed { 1.f };

	// Should wait for such time to resume if no occlusion is detected.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float ResumeWaitingTime { 0.2f };

	// When the transition process exceeds this percentage, will resume to the base pose ignoring any occlusion.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float DeadPercentage { 0.8f };

	// When the transition process exceeds this percentage, will resume to the base pose ignoring any occlusion.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta = (ClampMin = "0", ClampMax = "1"))
	float ResumeSpeed { 0.8f };

private:
	FVector PreviousOffset { FVector::ZeroVector };
	float ElapsedWaitingTime { 0.f };
	
	TArray<AActor*> ActorsToIgnore;
	EDrawDebugTrace::Type DrawDebugType;
};
