// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraTransitionTableFactory.generated.h"

/**
 * Factory for creating UComposableCameraTransitionTableDataAsset instances
 * from the Content Browser.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraTransitionTableFactory: public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraTransitionTableFactory(const FObjectInitializer& ObjectInitializer);

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
};
