// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetPins/SGraphPinObject.h"

class UScriptStruct;
struct FAssetData;

/**
 * DataTable asset pin for UK2Node_ActivateComposableCameraFromDataTable.
 *
 * Overrides GenerateAssetPicker so the asset picker only lists DataTable
 * assets whose row struct matches (or is a child of) the required row
 * struct from the K2 node. The required struct is obtained via the K2
 * node's static GetRequiredRowStruct() so the filter stays in lockstep
 * with whatever the node enforces at runtime.
 *
 * Why override GenerateAssetPicker instead of OnShouldFilterAsset:
 * SGraphPinObject's OnShouldFilterAsset is non-virtual, so a subclass
 * override never actually intercepts the filter - the base binds its
 * own method pointer into the FAssetPickerConfig. Replacing the whole
 * GenerateAssetPicker call and constructing our own picker config is
 * the only reliable way to install a custom filter. The asset-selected
 * and enter-pressed handlers forward to the base's protected virtuals
 * so the pin write + combo close still go through the canonical path.
 *
 * The filter itself runs in two stages: a fast asset-registry tag read
 * (against both RowStructurePath and RowStructure with multiple value
 * format comparisons), then a sync-load fallback via AssetData.GetAsset()
 * for the cases where the tag is missing or has an unexpected format.
 */
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API SGraphPinComposableCameraDataTable: public SGraphPinObject
{
public:
	SLATE_BEGIN_ARGS(SGraphPinComposableCameraDataTable) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	// SGraphPinObject 
	virtual TSharedRef<SWidget> GenerateAssetPicker() override;

private:
	/** Our custom filter - returns true to REJECT an asset. Matches the
	 * FOnShouldFilterAsset contract expected by FAssetPickerConfig. */
	bool ShouldFilterDataTable(const FAssetData& AssetData) const;

	/** Forwarders to the base class's protected virtuals. We need these
	 * so that asset selection still writes the pin value through the
	 * canonical path (pin modify, schema TrySetDefaultObject, combo
	 * close) without us having to reimplement any of it ourselves. */
	void HandleAssetSelected(const FAssetData& AssetData);
	void HandleAssetEnterPressed(const TArray<FAssetData>& InSelectedAssets);
};
