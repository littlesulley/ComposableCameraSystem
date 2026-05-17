// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Tickable.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "AsyncFloatCurveEvaluator.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTickFloatCurve, float, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCompleteFloatCurve);

/**
 * An async action evaluating a given float curve lasting for a given duration.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UAsyncFloatCurveEvaluator
	: public UBlueprintAsyncActionBase
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	UFUNCTION(BlueprintCallable, DisplayName = "Async Float Curve Evaluator", meta = (BlueprintInternalUseOnly="true", Category = "AsyncActions", WorldContext = "WorldContextObject"))
	static UAsyncFloatCurveEvaluator* AsyncFloatCurveEvaluator(UObject* WorldContextObject, UCurveFloat* Curve, float Duration);

	UPROPERTY(BlueprintAssignable, Category = "Async Action")
	FOnTickFloatCurve OnTick;

	UPROPERTY(BlueprintAssignable, Category = "Async Action")
	FOnCompleteFloatCurve OnComplete;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> Curve { nullptr };

	float Duration { 0.f };
	float ElapsedTime { 0.f };

	// Default OFF. The factory flips this to true only after Curve / Duration
	// have been assigned and the action is fully constructed, so a CDO / template
	// or partially-initialised instance is never tickable. See IsTickable for
	// the matching template / lifecycle guards.
	bool bShouldTick { false };
};
