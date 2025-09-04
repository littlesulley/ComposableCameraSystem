// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorFactories/ActorFactory.h"
#include "ComposableCameraVariableCollectionFactory.generated.h"

UCLASS(ClassGroup = ComposableCameraSystem, HideCategories = Object)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraVariableCollectionFactory
	: public UFactory
{
	GENERATED_BODY()

public:

	UComposableCameraVariableCollectionFactory(const FObjectInitializer& ObjectInit);

	// UFactory Interface
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
