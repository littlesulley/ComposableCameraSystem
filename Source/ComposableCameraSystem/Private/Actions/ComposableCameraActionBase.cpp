// Copyright 2026 Sulley. All Rights Reserved.

#include "Actions/ComposableCameraActionBase.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraPlayerCameraManager.h"

bool UComposableCameraActionBase::OnCanExecute(float DeltaTime, const FComposableCameraPose& /*CurrentCameraPose*/)
{
	bConditionCheckedThisUpdate = false;
	bCanExecuteCondition = true;
	bool bCanExecuteInstantThisTick { true };
	bool bCanExecuteDurationThisTick { true };
	bool bCanExecuteManulThisTick { true };
	
	if (ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Instant))
	{
		bCanExecuteInstantThisTick = bCanExecuteInstant;
		bCanExecuteInstant = false;
	}

	if (ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Duration))
	{
		if (Duration <= 0.f)
		{
			return false;
		}
		bCanExecuteDurationThisTick = bCanExecuteDuration;
		if (ElapsedTime += DeltaTime; ElapsedTime >= Duration)
		{
			bCanExecuteDuration = false;
		}
	}

	if (ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Manual))
	{
		bCanExecuteManulThisTick = bCanExecuteManual;
	}

	return bCanExecuteInstantThisTick && bCanExecuteDurationThisTick && bCanExecuteManulThisTick;
}

void UComposableCameraActionBase::ExecuteForCamera(AComposableCameraCameraBase* Camera,
	float DeltaTime, const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	if (!IsValid(Camera) || !IsValid(PlayerCameraManager)
		|| !PlayerCameraManager->GetCameraActions().Contains(this))
	{
		return;
	}

	// A persistent action can be bound to both sides of a blend: only the
	// running camera may expire it. A current-camera-only action stays bound
	// to its original camera, which may still tick as a transition source.
	const bool bOwnsCondition = bOnlyForCurrentCamera
		|| Camera == PlayerCameraManager->RunningCamera;
	if ((ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Condition))
		&& bOwnsCondition)
	{
		if (!bConditionCheckedThisUpdate)
		{
			bConditionCheckedThisUpdate = true;
			bCanExecuteCondition = CanExecute(DeltaTime, CurrentCameraPose);
		}
		if (!bCanExecuteCondition)
		{
			PlayerCameraManager->RemoveCameraAction(this);
			return;
		}
	}

	// CanExecute is Blueprint-authored and may remove this action itself.
	if (IsValid(PlayerCameraManager)
		&& PlayerCameraManager->GetCameraActions().Contains(this))
	{
		OnExecute(DeltaTime, CurrentCameraPose, OutCameraPose);
	}
}
