// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraShotTemplateLibraryFactory.generated.h"

UCLASS(ClassGroup = ComposableCameraSystem)
class UComposableCameraShotTemplateLibraryFactory : public UFactory
{
	GENERATED_BODY()
public:
	UComposableCameraShotTemplateLibraryFactory(const FObjectInitializer& Initializer);
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
		EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
};
