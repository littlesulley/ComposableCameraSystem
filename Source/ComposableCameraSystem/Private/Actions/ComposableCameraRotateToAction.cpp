// Copyright Sulley. All rights reserved.

#include "Actions/ComposableCameraRotateToAction.h"

UComposableCameraRotateToAction::UComposableCameraRotateToAction(FObjectInitializer& ObjectInitializer)
{
	ExecutionType = EComposableCameraActionExecutionType::PreCameraTick;
	ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Condition);
}

bool UComposableCameraRotateToAction::CanExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose)
{
	
}

void UComposableCameraRotateToAction::OnExecute_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	
}
