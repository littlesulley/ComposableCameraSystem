// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraParameterTableRow.generated.h"

class UComposableCameraTypeAsset;
class UComposableCameraTransitionDataAsset;

/**
 * Bag of serialized per-parameter values, keyed by the exposed parameter's
 * FName. Exists as a dedicated USTRUCT (rather than a naked TMap field on the
 * row) so the editor module can register an IPropertyTypeCustomization for it.
 *
 * WHY THIS WRAPPER EXISTS: UE's FStructureDetailsView. The panel used by the
 * DataTable editor to edit a row. Does NOT invoke IPropertyTypeCustomization
 * at the ROOT struct level. It only applies customizations to child struct
 * properties. If we customized FComposableCameraParameterTableRow directly,
 * the customization would never fire for DataTable row editing. Wrapping the
 * parameter map in this sub-struct gives us a child property that the details
 * view will route through our customization, and from there we walk up to
 * the parent row to find the sibling CameraType.
 *
 * At runtime, code reads Row.Parameters.Values directly. This struct exists
 * purely to give the editor a customization hook.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraExposedParameterValues
{
	GENERATED_BODY()

	/**
	 * Serialized per-parameter values keyed by the exposed parameter's FName.
	 *
	 * Not authored directly in the DataTable editor. The property-type
	 * customization generates a widget per parameter based on the parent row's
	 * selected CameraType and round-trips values through
	 * FComposableCameraParameterBlock::ApplyStringValue at activation time.
	 * Entries whose keys no longer correspond to any exposed parameter on the
	 * current CameraType are auto-removed by the customization.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TMap<FName, FString> Values;
};

/**
 * DataTable row describing a camera-type activation. One row = one callable
 * "camera preset": the camera type to activate, the context to activate into,
 * an optional transition override, pose preservation, and the serialized values
 * for each exposed parameter on that type.
 *
 * Parameter values are stored as strings inside FComposableCameraExposedParameterValues
 * (see the rationale above that struct for why there's a wrapper). At activation
 * time, each entry is parsed by FComposableCameraParameterBlock::ApplyStringValue
 * using the type info from the selected CameraType's GetExposedParameters().
 * This keeps the row schema fixed (one USTRUCT for the whole DataTable) while
 * still allowing each row to carry whatever parameter set its selected camera
 * type happens to declare.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraParameterTableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The camera type this row activates. Soft-referenced so DataTable assets
	 *  don't force-load every camera type in the project at boot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSoftObjectPtr<UComposableCameraTypeAsset> CameraType;

	/** Context to activate into. If NAME_None, the active context is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FName ContextName;

	/** Optional transition override. If null, the type asset's default
	 *  transition is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSoftObjectPtr<UComposableCameraTransitionDataAsset> TransitionOverride;

	/** Activation parameters forwarded to the context stack when this row is
	 *  used. Contains pose preservation, initial transform, transient settings,
	 *  etc.. Identical to the struct exposed on the K2 node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FComposableCameraActivateParams ActivationParams;

	/**
	 * Per-parameter values for this row's CameraType. The wrapper struct
	 * exists so the editor can hang an IPropertyTypeCustomization off it - customizations do not fire at the root of FStructureDetailsView, only
	 * on child struct properties.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "Exposed Parameters"))
	FComposableCameraExposedParameterValues Parameters;
};
