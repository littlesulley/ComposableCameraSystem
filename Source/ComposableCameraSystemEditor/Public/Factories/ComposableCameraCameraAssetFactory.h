// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraCameraAssetFactory.generated.h"

UCLASS(ClassGroup = ComposableCameraSystem, Experimental)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraCameraAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraCameraAssetFactory(const FObjectInitializer& ObjectInitializer);

	// UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ConfigureProperties() override;
	virtual bool ShouldShowInNewMenu() const override;
};
