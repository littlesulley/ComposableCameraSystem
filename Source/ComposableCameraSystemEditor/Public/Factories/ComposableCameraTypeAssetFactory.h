// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraTypeAssetFactory.generated.h"

/**
 * Factory for creating UComposableCameraTypeAsset instances from the Content Browser.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraTypeAssetFactory: public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraTypeAssetFactory(const FObjectInitializer& ObjectInitializer);

	// UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ConfigureProperties() override;
	virtual bool ShouldShowInNewMenu() const override;
};
