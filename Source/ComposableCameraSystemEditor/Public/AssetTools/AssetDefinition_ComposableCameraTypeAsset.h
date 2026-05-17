// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_ComposableCameraTypeAsset.generated.h"

/**
 * Asset definition for UComposableCameraTypeAsset.
 * Registers the camera type asset in the Content Browser with custom display name,
 * color, and category, and opens the visual node graph editor on double-click.
 */
UCLASS(ClassGroup = ComposableCameraSystemEditor)
class UAssetDefinition_ComposableCameraTypeAsset: public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual FAssetOpenSupport GetAssetOpenSupport(const FAssetOpenSupportArgs& OpenSupportArgs) const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
