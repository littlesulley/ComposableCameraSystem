// Copyright Sulley. All rights reserved.

#include "Factories/ComposableCameraPatchTypeAssetFactory.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"

UComposableCameraPatchTypeAssetFactory::UComposableCameraPatchTypeAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraPatchTypeAsset::StaticClass();
}

UObject* UComposableCameraPatchTypeAssetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UComposableCameraPatchTypeAsset* NewPatchAsset = NewObject<UComposableCameraPatchTypeAsset>(Parent, Class, Name, Flags | RF_Transactional);
	return NewPatchAsset;
}

bool UComposableCameraPatchTypeAssetFactory::ConfigureProperties()
{
	return true;
}

bool UComposableCameraPatchTypeAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}
