// Copyright 2026 Sulley. All Rights Reserved.

#include "Actions/ComposableCameraActionBase.h"

bool UComposableCameraActionBase::OnCanExecute(float DeltaTime, const FComposableCameraPose& CurrentCameraPose)
{
	bool bCanExecuteInstantThisTick { true };
	bool bCanExecuteDurationThisTick { true };
	bool bCanExecuteManulThisTick { true };
	bool bCanExecuteConditionThisTick { true };
	
	if (ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Instant))
	{
		bCanExecuteInstantThisTick = bCanExecuteInstant;
		bCanExecuteInstant = false;
	}

	if (ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Duration))
	{
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

	if (ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Condition))
	{
		bCanExecuteCondition = CanExecute(DeltaTime, CurrentCameraPose);
		bCanExecuteConditionThisTick = bCanExecuteCondition;
	}
	
	return bCanExecuteInstantThisTick && bCanExecuteDurationThisTick && bCanExecuteManulThisTick && bCanExecuteConditionThisTick;
}