// Copyright Sulley. All rights reserved.

#include "Factories/ComposableCameraVariableCollectionFactory.h"

#include "Variables/ComposableCameraVariableCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComposableCameraVariableCollectionFactory)

#define LOCTEXT_NAMESPACE "ComposableCameraVariableCollectionFactory"

UComposableCameraVariableCollectionFactory::UComposableCameraVariableCollectionFactory(
	const FObjectInitializer& ObjectInit)
		: Super(ObjectInit)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UComposableCameraVariableCollection::StaticClass();
}

FText UComposableCameraVariableCollectionFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Composable Camera Variable Collection");
}

UObject* UComposableCameraVariableCollectionFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
	EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UComposableCameraVariableCollection* Collection = NewObject<UComposableCameraVariableCollection>(Parent, Class, Name, Flags | RF_Transactional);
	return Collection;
}

#undef LOCTEXT_NAMESPACE