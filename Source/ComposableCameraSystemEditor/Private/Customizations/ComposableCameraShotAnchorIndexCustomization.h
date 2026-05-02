// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FPropertyEditorModule;
class IPropertyHandle;

/**
 * IPropertyTypeCustomization registered against BOTH
 * `FComposableCameraAnchorSpec` AND `FComposableCameraAnchorTargetWeight`.
 *
 * In each struct it intercepts the `TargetIndex` row and replaces the bare
 * integer spinner with the shared `FShotTargetIndexCombo` dropdown — same
 * combo used for `FShotPlacement::BasisActorIndex`. Walking up to the Shot's
 * Targets array handles both the AnchorSpec depth (Shot → Layer → Anchor →
 * TargetIndex) and the AnchorTargetWeight depth (Shot → Layer → Anchor →
 * WeightedTargets[i] → TargetIndex) uniformly.
 *
 * Per-struct visibility:
 *   - `FComposableCameraAnchorSpec`: TargetIndex only applies when
 *     `Mode == SingleTarget`. Mirror that with a manual Visibility attribute
 *     (custom rows don't auto-honor UPROPERTY `EditCondition`).
 *   - `FComposableCameraAnchorTargetWeight`: TargetIndex always applies
 *     when the entry exists — no gate.
 *
 * One class covers both because the only difference is the visibility
 * predicate and that's evaluated at render time. The CustomizeChildren
 * dispatch checks the struct type via the property handle's
 * `GetStructProperty()->Struct` to decide whether to install the
 * `Mode == SingleTarget` gate.
 */
class FShotAnchorIndexCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		class IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> StructHandle;
};
