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
	float Yaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "-90", ClampMax = "90"))
	float Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Length;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Radius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Offset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UCurveFloat* StrengthCurve;

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
