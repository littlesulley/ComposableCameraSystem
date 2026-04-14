// Copyright Sulley. All rights reserved.

#include "Factories/ComposableCameraTransitionFactory.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"

UComposableCameraTransitionFactory::UComposableCameraTransitionFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraTransitionDataAsset::StaticClass();
}

UObject* UComposableCameraTransitionFactory::FactoryCreateNew(
	UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UComposableCameraTransitionDataAsset>(Parent, Class, Name, Flags | RF_Transactional);
}

bool UComposableCameraTransitionFactory::ShouldShowInNewMenu() const
{
	return true;
}
