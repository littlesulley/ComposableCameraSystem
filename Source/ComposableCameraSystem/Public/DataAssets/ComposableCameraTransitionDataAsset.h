// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "ComposableCameraTransitionDataAsset.generated.h"

class UComposableCameraTransitionBase;

/**
 * Data asset for defining an instanced transition.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Transition")
	TObjectPtr<UComposableCameraTransitionBase> Transition;
};