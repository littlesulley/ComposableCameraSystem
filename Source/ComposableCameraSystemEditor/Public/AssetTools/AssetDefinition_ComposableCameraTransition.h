// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_ComposableCameraTransition.generated.h"

/**
 * Asset definition for UComposableCameraTransitionDataAsset.
 * Registers the transition data asset in the Content Browser under the
 * "Composable Camera System" category with a distinctive color.
 */
UCLASS(ClassGroup = ComposableCameraSystemEditor)
class UAssetDefinition_ComposableCameraTransition: public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};
