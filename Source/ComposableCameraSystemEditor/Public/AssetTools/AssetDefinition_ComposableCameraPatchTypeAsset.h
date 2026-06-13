// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "AssetDefinition_ComposableCameraPatchTypeAsset.generated.h"

/**
 * Asset definition for UComposableCameraPatchTypeAsset.
 *
 * Parallel to UAssetDefinition_ComposableCameraTypeAsset - Patch IS-A TypeAsset
 * via inheritance, so without this Patch instances would fall back to the
 * TypeAsset's asset definition (wrong display name, wrong color, wrong category
 * pivot for users browsing). Registering a Patch-specific definition overrides
 * the inherited match for Patch instances.
 *
 * The OpenAssets handler still routes to CreateComposableCameraTypeAssetEditor - 
 * the Patch reuses the existing visual graph editor unchanged
 * (PatchSystemProposal Section 5 / Section 16.8).
 */
UCLASS(ClassGroup = ComposableCameraSystemEditor)
class UAssetDefinition_ComposableCameraPatchTypeAsset: public UAssetDefinitionDefault
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
