// Copyright 2026 Sulley. All Rights Reserved.

#include "Factories/ComposableCameraTypeAssetFactory.h"
#include "DataAssets/ComposableCameraTypeAsset.h"

UComposableCameraTypeAssetFactory::UComposableCameraTypeAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraTypeAsset::StaticClass();
}

UObject* UComposableCameraTypeAssetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UComposableCameraTypeAsset* NewTypeAsset = NewObject<UComposableCameraTypeAsset>(Parent, Class, Name, Flags | RF_Transactional);
	return NewTypeAsset;
}

bool UComposableCameraTypeAssetFactory::ConfigureProperties()
{
	return true;
}

bool UComposableCameraTypeAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}
