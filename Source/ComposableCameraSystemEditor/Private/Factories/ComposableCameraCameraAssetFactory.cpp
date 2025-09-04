// Copyright Sulley. All rights reserved.

#include "Factories/ComposableCameraCameraAssetFactory.h"
#include "Core/ComposableCameraCameraAsset.h"

UComposableCameraCameraAssetFactory::UComposableCameraCameraAssetFactory(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraCameraAsset::StaticClass();
}

UObject* UComposableCameraCameraAssetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UComposableCameraCameraAsset* NewCameraAsset = NewObject<UComposableCameraCameraAsset>(Parent, Class, Name, Flags | RF_Transactional);
	return NewCameraAsset;
}

bool UComposableCameraCameraAssetFactory::ConfigureProperties()
{
	return true;
}

bool UComposableCameraCameraAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}
