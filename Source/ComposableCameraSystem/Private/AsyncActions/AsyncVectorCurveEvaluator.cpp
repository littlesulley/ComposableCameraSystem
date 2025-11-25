// Copyright Sulley. All rights reserved.

#include "AsyncActions/AsyncVectorCurveEvaluator.h"

void UAsyncVectorCurveEvaluator::Tick(float DeltaTime)
{
	if (!Curve)
	{
		OnComplete.Broadcast();
		SetReadyToDestroy();
		bShouldTick = false;
		return;
	}
	
	ElapsedTime = FMath::Clamp(ElapsedTime + DeltaTime, 0.f, Duration);
	FVector Value = Curve->GetVectorValue(ElapsedTime);
	OnTick.Broadcast(Value);

	if (ElapsedTime >= Duration)
	{
		OnComplete.Broadcast();
		SetReadyToDestroy();
		bShouldTick = false;
	}
}

TStatId UAsyncVectorCurveEvaluator::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncVectorCurveEvaluatorStat, STATGROUP_Tickables);
}

bool UAsyncVectorCurveEvaluator::IsTickable() const
{
	return bShouldTick;
}

UAsyncVectorCurveEvaluator* UAsyncVectorCurveEvaluator::AsyncVectorCurveEvaluator(UObject* WorldContextObject,
	UCurveVector* Curve, float Duration)
{
	UAsyncVectorCurveEvaluator* Action = NewObject<UAsyncVectorCurveEvaluator>();
	
	Action->RegisterWithGameInstance(WorldContextObject);
	Action->Curve = Curve;
	Action->Duration = Duration;
	return Action;
}
