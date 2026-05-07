// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Curves/CurveVector.h"
#include "AsyncVectorCurveEvaluator.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTickVectorCurve, FVector, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCompleteVectorCurve);

/**
 * An async action evaluating a given vector curve lasting for a given duration.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UAsyncVectorCurveEvaluator
	: public UBlueprintAsyncActionBase
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	UFUNCTION(BlueprintCallable, DisplayName = "Async Vector Curve Evaluator", meta = (BlueprintInternalUseOnly="true", Category = "AsyncActions", WorldContext = "WorldContextObject"))
	static UAsyncVectorCurveEvaluator* AsyncVectorCurveEvaluator(UObject* WorldContextObject, UCurveVector* Curve, float Duration);

	UPROPERTY(BlueprintAssignable)
	FOnTickVectorCurve OnTick;

	UPROPERTY(BlueprintAssignable)
	FOnCompleteVectorCurve OnComplete;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UCurveVector> Curve { nullptr };

	float Duration { 0.f };
	float ElapsedTime { 0.f };

	// Default OFF. The factory flips this to true only after Curve / Duration
	// have been assigned and the action is fully constructed, so a CDO / template
	// or partially-initialised instance is never tickable. See IsTickable for
	// the matching template / lifecycle guards.
	bool bShouldTick { false };
};
