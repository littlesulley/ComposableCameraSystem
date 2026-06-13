// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FPropertyEditorModule;
class IPropertyHandle;

/**
 * Hides inactive `FComposableCameraShot` layer fields in Details.
 *
 * Registered for `FShotAim`, `FShotLens`, and `FShotFocus`. The data model
 * keeps every field serialized; this customization only collapses rows whose
 * value is ignored by the active mode enum.
 */
class FShotLayerModeCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		class IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructHandle;
};
