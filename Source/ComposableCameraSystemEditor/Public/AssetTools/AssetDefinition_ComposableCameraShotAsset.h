// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_ComposableCameraShotAsset.generated.h"

/**
 * Asset definition for `UComposableCameraShotAsset`.
 *
 * Provides display name, color, and category for the Content Browser entry,
 * and routes `OpenAssets` into the Shot Editor (Phase D) so double-clicking
 * a ShotAsset opens the same authoring tool that LS Shot Sections in
 * AssetReference mode use. Treats the ShotAsset itself as the host UObject - 
 * `FNotifyHook` edits on the Shot struct flow through `Modify()` /
 * `PostEditChangeProperty` on the asset, so the asset is properly marked
 * dirty and its undo stack is populated.
 */
UCLASS(ClassGroup = ComposableCameraSystemEditor)
class UAssetDefinition_ComposableCameraShotAsset: public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
