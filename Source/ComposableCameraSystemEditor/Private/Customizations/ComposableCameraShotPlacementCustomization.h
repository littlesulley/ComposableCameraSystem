// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FPropertyEditorModule;
class IPropertyHandle;

/**
 * IPropertyTypeCustomization for `FShotPlacement`.
 *
 * Replaces the default integer-spinner rendering of `BasisActorIndex` with
 * a dropdown of valid `Shot.Targets` indices, each labeled
 * `"<i> - <ActorLabel>"`. Reordering Targets no longer silently breaks the
 * index reference because the dropdown reads the live array each time it's
 * opened - the displayed indices always match the current Targets layout.
 *
 * All other `FShotPlacement` fields are default-rendered, but still receive
 * mode-sensitive visibility gates so Details shows only rows consumed by the
 * active `Mode`.
 *
 * Visibility gating: `AddCustomRow` does not auto-evaluate the field's
 * UPROPERTY `EditCondition` meta the way `AddProperty` does, so the
 * intercepted row attaches a manual `Visibility` attribute that mirrors the
 * authored condition (`Mode == AnchorOrbit && BasisFrame == InheritFromActor`).
 * The row collapses out of the panel when the field is irrelevant - chosen
 * over `IsEnabled` (greying) because `BasisActorIndex` has zero effect in
 * non-AnchorOrbit modes, and a greyed-but-present row would just clutter
 * the Placement category.
 *
 * Combo widget itself comes from the shared `FShotTargetIndexCombo` helper - 
 * same widget used for `FComposableCameraAnchorSpec::TargetIndex` and
 * `FComposableCameraAnchorTargetWeight::TargetIndex`. Visibility gating is
 * specific to BasisActorIndex (Mode + BasisFrame conjunction) and lives
 * here.
 *
 * Lives in the editor module and registers against `FShotPlacement::StaticStruct()`.
 */
class FShotPlacementCustomization: public IPropertyTypeCustomization
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
