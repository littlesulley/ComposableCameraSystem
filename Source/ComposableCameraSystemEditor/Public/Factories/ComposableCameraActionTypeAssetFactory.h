// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraActionTypeAssetFactory.generated.h"

UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraActionTypeAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraActionTypeAssetFactory(const FObjectInitializer& ObjectInitializer);
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
		EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
