// Copyright 2026 Sulley. All Rights Reserved.

#include "Actions/ComposableCameraMoveToAction.h"
#include "Core/ComposableCameraPlayerCameraManager.h"

UComposableCameraMoveToAction::UComposableCameraMoveToAction(const FObjectInitializer& ObjectInitializer)
{
	ExecutionType = EComposableCameraActionExecutionType::PostCameraTick;
	ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);
}

bool UComposableCameraMoveToAction::CanExecute_Implementation(float DeltaTime,
                                                              const FComposableCameraPose& CurrentCameraPose)
{
	return !CurrentCameraPose.Position.Equals(TargetPosition);
}

void UComposableCameraMoveToAction::OnExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OutCameraPose.Position = FMath::VInterpTo(
		CurrentCameraPose.Position,
		TargetPosition,
		DeltaTime,
		MoveSpeed
	);
}
