// Copyright Sulley. All rights reserved.

#include "Factories/ComposableCameraTransitionTableFactory.h"
#include "DataAssets/ComposableCameraTransitionTableDataAsset.h"

UComposableCameraTransitionTableFactory::UComposableCameraTransitionTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraTransitionTableDataAsset::StaticClass();
}

UObject* UComposableCameraTransitionTableFactory::FactoryCreateNew(
	UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UComposableCameraTransitionTableDataAsset>(Parent, Class, Name, Flags | RF_Transactional);
}

bool UComposableCameraTransitionTableFactory::ShouldShowInNewMenu() const
{
	return true;
}
