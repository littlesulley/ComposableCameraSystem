// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FPropertyEditorModule;

/**
 * Property-type customization for FComposableCameraTypeAssetReference.
 *
 * Shows a warning banner above the standard child rows (TypeAsset / Parameters /
 * Variables) when the bound TypeAsset contains nodes whose
 * EComposableCameraNodeLevelSequenceCompatibility is anything other than
 * Compatible. Two categories of warning:
 *
 * - ComputeOnly nodes: Compute chain is skipped entirely in the LS
 * evaluation path. The warning tells the designer that any value those nodes
 * would publish must be re-sourced as an exposed parameter for LS playback.
 *
 * - RequiresPCM nodes: node initialization / ticking needs a
 * PlayerCameraManager. These nodes are guarded in LS and become silent no-ops,
 * so the designer should be told. The banner lists each offending node class.
 *
 * Registered once per module startup alongside the other detail customizations
 * (see FComposableCameraSystemEditorModule::RegisterDetailsCustomizations).
 */
class FComposableCameraTypeAssetReferenceCustomization: public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	// IPropertyTypeCustomization.
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& Utils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& Utils) override;
};
