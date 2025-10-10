// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraCylindricalTransition.h"

#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Math/ComposableCameraMath.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

void UComposableCameraCylindricalTransition::OnBeginPlay_Implementation(float DeltaTime,
                                                                        const FComposableCameraPose& CurrentTargetPose)
{
	Interpolator_T = PivotInterpolator ? PivotInterpolator->BuildVector3dInterpolator() : nullptr;
}

FComposableCameraPose UComposableCameraCylindricalTransition::OnEvaluate_Implementation(float DeltaTime,
                                                                                        const FComposableCameraPose& CurrentTargetPose)
{
	// Position.
	FComposableCameraRayDefinition StartRay { StartCameraPose.Position, StartCameraPose.Rotation.RotateVector(FVector::ForwardVector), MinimumDistanceFromOrigin };
	FComposableCameraRayDefinition TargetRay { CurrentTargetPose.Position, CurrentTargetPose.Rotation.RotateVector(FVector::ForwardVector), MinimumDistanceFromOrigin };
	FComposableCameraNearestPointsOnRaysResult Result = StartRay.FindNearestPointsByOtherRay(TargetRay);

	float BlendPct = (TransitionTime - RemainingTime) / TransitionTime;
	BlendPct = ComposableCameraSystem::SmootherStep(BlendPct);

	FVector StartPosition = StartCameraPose.Position;
	FVector StartPivot = Result.FirstPoint;
	FVector TargetPosition = CurrentTargetPose.Position;
	FVector TargetPivot = Result.SecondPoint;
	
	FVector StartDirection = FVector::VectorPlaneProject(StartPosition - StartPivot, FVector::UpVector);
	FVector TargetDirection = FVector::VectorPlaneProject(TargetPosition - TargetPivot, FVector::UpVector);
	FVector ResultVector = ComposableCameraSystem::Slerp(StartDirection, TargetDirection, BlendPct);
		
	FVector StartResultPosition = StartPosition + (ResultVector - StartDirection);
	FVector TargetResultPosition = TargetPosition + (ResultVector - TargetDirection);
	FVector ResultPosition = FMath::Lerp(StartResultPosition, TargetResultPosition, BlendPct);

	// Rotation, always looking at the blended pivot.
	FVector ResultPivot = FMath::Lerp(StartPivot, TargetPivot, BlendPct);

	if (bFirstFrame)
	{
		LastPivot = StartPivot;
	}
	if (Interpolator_T)
	{
		Interpolator_T->Reset(LastPivot, TargetPivot);
		ResultPivot = Interpolator_T->Run(DeltaTime);
	}
	LastPivot = ResultPivot;

	FRotator ResultRotation = UKismetMathLibrary::FindLookAtRotation(ResultPosition, ResultPivot);

	// Returns output pose.
	FComposableCameraPose ResultPose {};
	ResultPose.Position = ResultPosition;
	ResultPose.Rotation = ResultRotation;
	ResultPose.FieldOfView = FMath::Lerp(StartCameraPose.FieldOfView, CurrentTargetPose.FieldOfView, BlendPct);

	// Draw debug info.
	if (AComposableCameraPlayerCamaraManager* PCM = UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(this, 0))
	{
		if (PCM->bDrawDebugInformation)
		{
			UKismetSystemLibrary::DrawDebugSphere(this, StartPivot, 20, 10, FLinearColor::Green, 0.f, 1.f);
			UKismetSystemLibrary::DrawDebugSphere(this, TargetPivot, 20, 10, FLinearColor::Blue, 0.f, 1.f);
			UKismetSystemLibrary::DrawDebugSphere(this, ResultPivot, 20, 10, FLinearColor::Red, 0.f, 1.f);
			UKismetSystemLibrary::DrawDebugString(this, StartPivot + FVector(0, 0, 30), "Start Pivot", nullptr, FLinearColor::Green, 0.f);
			UKismetSystemLibrary::DrawDebugString(this, TargetPivot + FVector(0, 0, 30), "Target Pivot", nullptr, FLinearColor::Blue, 0.f);
			UKismetSystemLibrary::DrawDebugString(this, ResultPivot + FVector(0, 0, 30), "Result Pivot", nullptr, FLinearColor::Red, 0.f);
		}
	}
	
	return ResultPose;
}
