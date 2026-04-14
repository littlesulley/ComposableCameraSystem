// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyUtilities;
class IStructureDetailsView;
class FPropertyEditorModule;
class FStructOnScope;
enum class EComposableCameraPinType : uint8;

/**
 * Property type customization for FComposableCameraInternalVariable.
 *
 * Replaces the raw InitialValueString text box with a type-aware widget
 * that matches the variable's VariableType. For example, Float gets a
 * numeric spinner, Vector3D gets three labeled numeric fields, Bool gets
 * a checkbox, etc. When the VariableType changes, the customization is
 * rebuilt so the widget adapts automatically.
 *
 * Transform gets a three-row layout (Location / Rotation / Scale) with
 * per-component spinners. Struct types with a known UScriptStruct get
 * a full inline struct editor (IStructureDetailsView) backed by
 * FStructOnScope, with bidirectional serialization through ImportText /
 * ExportText. Types that have no meaningful inline widget (Actor, Object)
 * fall back to a label.
 */
class FComposableCameraInternalVariableCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& Utils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& Utils) override;

private:
	/** Build a type-appropriate value widget for InitialValueString. */
	TSharedRef<SWidget> BuildTypedDefaultValueWidget(
		TSharedPtr<IPropertyHandle> InitialValueHandle,
		EComposableCameraPinType PinType,
		UScriptStruct* StructType);

	/** Build a 3-row (Location / Rotation / Scale) widget for Transform. */
	TSharedRef<SWidget> BuildTransformWidget(TSharedPtr<IPropertyHandle> InitialValueHandle);

	/** Build an inline struct editor for Struct types with a known UScriptStruct.
	 *  Creates an FStructOnScope, parses the current InitialValueString into it,
	 *  and adds the IStructureDetailsView's widget to ChildBuilder. Changes in
	 *  the struct editor are serialized back via ExportText. */
	void BuildStructDefaultValueRows(
		IDetailChildrenBuilder& ChildBuilder,
		TSharedPtr<IPropertyHandle> InitialValueHandle,
		UScriptStruct* InStructType);

	/** Helpers for multi-component vector/rotator types. */
	TSharedRef<SWidget> BuildNumericComponentWidget(
		TSharedPtr<IPropertyHandle> InitialValueHandle,
		int32 ComponentIndex,
		int32 NumComponents,
		const TCHAR* const* ComponentLabels,
		const TCHAR* Prefix);

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Kept alive so the IStructureDetailsView can edit the struct memory. */
	TSharedPtr<FStructOnScope> StructDefaultValueScope;

	/** Held to keep the view and its delegates alive. */
	TSharedPtr<IStructureDetailsView> StructDefaultValueView;
};
