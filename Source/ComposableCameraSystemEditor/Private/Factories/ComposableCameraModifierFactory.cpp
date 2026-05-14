// Copyright Sulley. All rights reserved.

#include "Factories/ComposableCameraModifierFactory.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"

UComposableCameraModifierFactory::UComposableCameraModifierFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraNodeModifierDataAsset::StaticClass();
}

UObject* UComposableCameraModifierFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UComposableCameraNodeModifierDataAsset>(Parent, Class, Name, Flags | RF_Transactional);
}

bool UComposableCameraModifierFactory::ShouldShowInNewMenu() const
{
	return true;
}
