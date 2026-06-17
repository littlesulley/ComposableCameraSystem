// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraCompositionPreservingTransition.generated.h"

class AActor;

/**
 * Rebuilds the source side of a transition so a moving subject keeps its
 * transition-start composition while rotation follows a driving transition.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCompositionPreservingTransition
	: public UComposableCameraTransitionBase
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlay_Implementation(
		float DeltaTime,
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose) override;

	virtual FComposableCameraPose OnEvaluate_Implementation(
		float DeltaTime,
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose) override;

	virtual float GetBlendWeightAt(float NormalizedTime) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const override;
#endif

public:
	/** Transition that drives R' and the non-rotation blend percentage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Transition")
	TObjectPtr<UComposableCameraTransitionBase> DrivingTransition { nullptr };

	/** Subject whose initial source-camera composition should be preserved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition|Composition")
	EComposableCameraActorInputSource SubjectActorSource { EComposableCameraActorInputSource::ControllerControlledPawn };

	/** Explicit subject when SubjectActorSource is ExplicitActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition|Composition", meta = (EditCondition = "SubjectActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> SubjectActor { nullptr };

private:
	AActor* ResolveSubjectActor() const;
	bool CaptureInitialSubjectComposition(const FComposableCameraPose& CurrentSourcePose);
	FComposableCameraPose BuildCompositionPreservingPose(
		const FComposableCameraPose& CurrentSourcePose,
		const FComposableCameraPose& CurrentTargetPose,
		const FVector& CurrentSubjectLocation,
		const FRotator& DrivingRotation,
		float BlendWeight) const;

private:
	FVector CapturedSubjectOffsetInSourceSpace { FVector::ZeroVector };
	bool bHasCapturedSubjectComposition { false };
};
