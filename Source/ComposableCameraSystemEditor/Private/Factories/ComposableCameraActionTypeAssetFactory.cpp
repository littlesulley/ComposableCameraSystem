// Copyright 2026 Sulley. All Rights Reserved.

#include "Factories/ComposableCameraActionTypeAssetFactory.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"

UComposableCameraActionTypeAssetFactory::UComposableCameraActionTypeAssetFactory(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraActionTypeAsset::StaticClass();
}

UObject* UComposableCameraActionTypeAssetFactory::FactoryCreateNew(
	UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UComposableCameraActionTypeAsset>(
		Parent, Class, Name, Flags | RF_Transactional);
}
