// Copyright 2026 Sulley. All Rights Reserved.

#include "Factories/ComposableCameraShotAssetFactory.h"
#include "DataAssets/ComposableCameraShotAsset.h"

UComposableCameraShotAssetFactory::UComposableCameraShotAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraShotAsset::StaticClass();
}

UObject* UComposableCameraShotAssetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UComposableCameraShotAsset>(Parent, Class, Name, Flags | RF_Transactional);
}

bool UComposableCameraShotAssetFactory::ConfigureProperties()
{
	return true;
}

bool UComposableCameraShotAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}
