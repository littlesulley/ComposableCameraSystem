// Copyright Sulley. All rights reserved.

#include "Transitions/ComposableCameraCylindricalTransition.h"

#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/KismetMathLibrary.h"

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

	FVector StartPosition = StartCameraPose.Position;
	FVector StartPivot = Result.FirstPoint;
	FVector TargetPosition = CurrentTargetPose.Position;
	FVector TargetPivot = Result.SecondPoint;

	FVector StartDirection = StartPosition - StartPivot;
	FVector TargetDirection = TargetPosition - TargetPivot;
	
	FVector StartProjectedDirection = StartDirection.ProjectOnToNormal(FVector::UpVector);
	FVector TargetProjectedDirection = TargetDirection.ProjectOnToNormal(FVector::UpVector);
	float StartRadius = StartProjectedDirection.Length();
	float TargetRadius = TargetProjectedDirection.Length();

	FVector StartProjectedNormalizedDirection = StartProjectedDirection.GetSafeNormal();
	FVector TargetProjectedNormalizedDirection = TargetProjectedDirection.GetSafeNormal();
	
	FVector ResultVector = FVector::SlerpNormals(StartProjectedNormalizedDirection, TargetProjectedNormalizedDirection, BlendPct);
	ResultVector *= StartRadius + BlendPct * (TargetRadius - StartRadius);
	FVector StartResultPosition = StartPosition + (ResultVector - StartProjectedDirection);
	FVector TargetResultPosition = TargetPosition + (ResultVector - TargetProjectedDirection);
	FVector ResultPosition = StartResultPosition + BlendPct * (TargetResultPosition - StartResultPosition);

	// Rotation, always looking to the blended pivot.
	FVector ResultPivot = StartPivot + BlendPct * (TargetPivot - StartPivot);
	
	if (Interpolator_T)
	{
		Interpolator_T->Reset(StartPivot, TargetPivot);
		ResultPivot = Interpolator_T->Run(DeltaTime);
	}

	FRotator ResultRotation = UKismetMathLibrary::FindLookAtRotation(ResultPosition, ResultPivot);

	// Returns output pose.
	FComposableCameraPose ResultPose {};
	ResultPose.Position = ResultPosition;
	ResultPose.Rotation = ResultRotation;
	ResultPose.FieldOfView = StartCameraPose.FieldOfView + BlendPct * (CurrentTargetPose.FieldOfView - StartCameraPose.FieldOfView);
	
	return ResultPose;
}
