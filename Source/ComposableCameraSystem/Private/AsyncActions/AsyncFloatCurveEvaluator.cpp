// Copyright Sulley. All rights reserved.

#include "AsyncActions/AsyncFloatCurveEvaluator.h"

void UAsyncFloatCurveEvaluator::Tick(float DeltaTime)
{
	if (!Curve)
	{
		OnComplete.Broadcast();
		SetReadyToDestroy();
		bShouldTick = false;
		return;
	}
	
	ElapsedTime = FMath::Clamp(ElapsedTime + DeltaTime, 0.f, Duration);
	float Value = Curve->GetFloatValue(ElapsedTime);
	OnTick.Broadcast(Value);

	if (ElapsedTime >= Duration)
	{
		OnComplete.Broadcast();
		SetReadyToDestroy();
		bShouldTick = false;
	}
}

TStatId UAsyncFloatCurveEvaluator::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncFloatCurveEvaluatorStat, STATGROUP_Tickables);
}

bool UAsyncFloatCurveEvaluator::IsTickable() const
{
	// FTickableGameObject ticks every registered object including the CDO and
	// archetype templates. Without these guards the CDO would enter Tick with
	// Curve == nullptr, fire OnComplete on whatever delegates the CDO has and
	// call SetReadyToDestroy on a class default. Undefined behaviour.
	// bShouldTick is also gated false until the factory has finished assigning
	// Curve / Duration so a partially-initialised instance is never ticked.
	return bShouldTick
		&& !IsTemplate()
		&& !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
		&& !IsUnreachable();
}

UAsyncFloatCurveEvaluator* UAsyncFloatCurveEvaluator::AsyncFloatCurveEvaluator(UObject* WorldContextObject,
                                                                               UCurveFloat* Curve, float Duration)
{
	UAsyncFloatCurveEvaluator* Action = NewObject<UAsyncFloatCurveEvaluator>();

	Action->RegisterWithGameInstance(WorldContextObject);
	Action->Curve = Curve;
	Action->Duration = Duration;
	// Flip last so IsTickable cannot return true on a half-constructed action.
	Action->bShouldTick = true;
	return Action;
}
