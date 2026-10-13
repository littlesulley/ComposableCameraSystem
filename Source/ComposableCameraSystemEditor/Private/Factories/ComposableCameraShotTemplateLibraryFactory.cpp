// Copyright 2026 Sulley. All Rights Reserved.
#include "Factories/ComposableCameraShotTemplateLibraryFactory.h"
#include "DataAssets/ComposableCameraShotTemplateLibrary.h"

UComposableCameraShotTemplateLibraryFactory::UComposableCameraShotTemplateLibraryFactory(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraShotTemplateLibrary::StaticClass();
}

UObject* UComposableCameraShotTemplateLibraryFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UComposableCameraShotTemplateLibrary>(Parent, Class, Name, Flags | RF_Transactional);
}

bool UComposableCameraShotTemplateLibraryFactory::ShouldShowInNewMenu() const
{
	// The asset definition supplies the shared Composable Camera System category.
	return true;
}
