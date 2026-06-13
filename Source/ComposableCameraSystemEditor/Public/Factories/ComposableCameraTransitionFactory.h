// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraTransitionFactory.generated.h"

/**
 * Factory for creating UComposableCameraTransitionDataAsset instances
 * from the Content Browser.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraTransitionFactory: public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraTransitionFactory(const FObjectInitializer& ObjectInitializer);

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
};
