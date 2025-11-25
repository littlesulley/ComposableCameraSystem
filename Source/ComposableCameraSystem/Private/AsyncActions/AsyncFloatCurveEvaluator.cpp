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
	return bShouldTick;
}

UAsyncFloatCurveEvaluator* UAsyncFloatCurveEvaluator::AsyncFloatCurveEvaluator(UObject* WorldContextObject,
                                                                               UCurveFloat* Curve, float Duration)
{
	UAsyncFloatCurveEvaluator* Action = NewObject<UAsyncFloatCurveEvaluator>();
	
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->Curve = Curve;
	Action->Duration = Duration;
	return Action;
}
