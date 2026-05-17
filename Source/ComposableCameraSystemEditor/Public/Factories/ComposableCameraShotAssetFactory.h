// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ComposableCameraShotAssetFactory.generated.h"

/**
 * Factory for creating UComposableCameraShotAsset instances from the Content Browser.
 *
 * Mirrors `UComposableCameraPatchTypeAssetFactory` minus the graph-editor wiring
 * (ShotAsset has no node graph - it is a pure data envelope). The "Composable
 * Camera Shot" entry appears in the Content Browser's New menu under the
 * "Composable Camera System" category.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraShotAssetFactory: public UFactory
{
	GENERATED_BODY()

public:
	UComposableCameraShotAssetFactory(const FObjectInitializer& ObjectInitializer);

	// UFactory.
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* Parent, FName Name,
		EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ConfigureProperties() override;
	virtual bool ShouldShowInNewMenu() const override;
};
