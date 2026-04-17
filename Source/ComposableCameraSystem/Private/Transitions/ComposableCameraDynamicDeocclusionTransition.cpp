// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraDynamicDeocclusionTransition.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "EditorHooks/EditorHooks.h"
#include "Kismet/GameplayStatics.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

void UComposableCameraDynamicDeocclusionTransition::OnBeginPlay_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (DrivingTransition)
	{
		DrivingTransition->TransitionEnabled(InitParams);
		DrivingTransition->SetTransitionTime(TransitionTime);
		DrivingTransition->ResetTransitionState();
	}

	// DrawDebugType.
	DrawDebugType = EDrawDebugTrace::None;
#if ENABLE_DRAW_DEBUG
	if (AComposableCameraPlayerCameraManager* PCM = UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(this, 0))
	{
		if (PCM->bDrawDebugInformation)
		{
			DrawDebugType = EDrawDebugTrace::ForOneFrame;
		}
	}

#if WITH_EDITOR
	if (!FIsSimulatingInEditor::GetIsSimulatingInEditor())
	{
		DrawDebugType = EDrawDebugTrace::None;
	}
#endif
#endif

	// Ignored actors.
	for (TSoftClassPtr<AActor> ActorType: ActorTypesToIgnore)
	{
		if (ActorType.IsValid())
		{
			TArray<AActor*> IgnoredActors;
			UGameplayStatics::GetAllActorsOfClass(this, ActorType.Get(), IgnoredActors);
			ActorsToIgnore.Append(IgnoredActors);
		}
	}
}

FComposableCameraPose UComposableCameraDynamicDeocclusionTransition::OnEvaluate_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentSourcePose, const FComposableCameraPose& CurrentTargetPose)
{
	if (!DrivingTransition)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("DrivingTransition is not valid in ComposableCameraDynamicDeocclusionTransition."));
		return FComposableCameraPose{};
	}

	FComposableCameraPose BasePose = DrivingTransition->Evaluate(DeltaTime, CurrentSourcePose, CurrentTargetPose);
	FComposableCameraPose CandidatePose = BasePose;
	CandidatePose.Position = PreviousOffset + BasePose.Position;
	FVector AggregateOffset = FVector::ZeroVector;

	// For each feeler.
	for (const auto& Feeler : Feelers)
	{
		FVector StartPosition = Feeler.GetRayStartPosition(CandidatePose);
		FVector EndPosition = Feeler.GetRayEndPosition(CandidatePose);

		FHitResult Hit;
		UKismetSystemLibrary::SphereTraceSingle(
			this,
			StartPosition,
			EndPosition,
			Feeler.Radius,
			TraceChannel,
			true,
			ActorsToIgnore,
			DrawDebugType,
			Hit,
			true);

		if (Hit.bBlockingHit && Feeler.StrengthCurve)
		{
			FVector Offset = CandidatePose.Rotation.RotateVector(Feeler.Offset.GetSafeNormal());
			AggregateOffset += Offset * Feeler.StrengthCurve->GetFloatValue(Hit.Distance);
		}
	}

	if (AggregateOffset.IsZero())
	{
		ElapsedWaitingTime += DeltaTime;
	}
	else
	{
		ElapsedWaitingTime = 0.f;
	}

	if (ElapsedWaitingTime >= ResumeWaitingTime || (GetTransitionTime() - GetRemainingTime()) / GetTransitionTime() >= DeadPercentage)
	{
		AggregateOffset = FMath::VInterpTo(
			PreviousOffset,
			FVector::ZeroVector,
			DeltaTime,
			DeocclusionSpeed
		);
	}
	else
	{
		AggregateOffset = FMath::VInterpTo(
			PreviousOffset,
			PreviousOffset + AggregateOffset,
			DeltaTime,
			DeocclusionSpeed
		);
	}
	
	CandidatePose.Position = BasePose.Position + AggregateOffset;
	PreviousOffset = AggregateOffset;

	Percentage = DrivingTransition->GetPercentage();
	return CandidatePose;
}
