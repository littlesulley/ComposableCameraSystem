// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraPatchTypeAssetFactory.generated.h"

/**
 * Factory for creating UComposableCameraPatchTypeAsset instances from the Content Browser.
 *
 * Parallel to UComposableCameraTypeAssetFactory - separate factory so the
 * "Camera Patch" entry appears as its own item in the New menu and the asset
 * type is unambiguous from the Content Browser. The asset itself reuses the
 * UComposableCameraTypeAsset visual editor unchanged (PatchSystemProposal Section 5).
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraPatchTypeAssetFactory: public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraPatchTypeAssetFactory(const FObjectInitializer& ObjectInitializer);

	// UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ConfigureProperties() override;
	virtual bool ShouldShowInNewMenu() const override;
};
