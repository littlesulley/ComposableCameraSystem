// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyUtilities;
class IStructureDetailsView;
class FPropertyEditorModule;
class FStructOnScope;
class UEnum;
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
	/** Build a type-appropriate value widget for InitialValueString.
	 *  EnumType is consulted only when PinType == Enum; ignored otherwise. */
	TSharedRef<SWidget> BuildTypedDefaultValueWidget(
		TSharedPtr<IPropertyHandle> InitialValueHandle,
		EComposableCameraPinType PinType,
		UScriptStruct* StructType,
		UEnum* EnumType);

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

	/** Custom picker for the EnumType field. The default property widget for
	 *  TObjectPtr<UEnum> shows the asset picker filtered to UUserDefinedEnum
	 *  assets only -- native `UENUM(BlueprintType)` enums (defined in C++)
	 *  never appear, leaving the dropdown empty in projects without any
	 *  BP-defined enums. This builds a combo button whose menu walks every
	 *  loaded UEnum that opts into BlueprintType, surfacing both BP-defined
	 *  and native enums uniformly. */
	TSharedRef<SWidget> BuildEnumTypePicker(TSharedPtr<IPropertyHandle> EnumTypeHandle);

	/** Combo button menu content -- walks TObjectIterator<UEnum>, filters for
	 *  BlueprintType + non-deprecated, and emits one menu entry per matching
	 *  enum that writes the picked UEnum back through the handle. */
	TSharedRef<SWidget> BuildEnumTypeMenu(TSharedPtr<IPropertyHandle> EnumTypeHandle);

	/** Combo button label text -- reads the current EnumType value from the
	 *  handle and returns its display name (or "None" when unset). */
	FText GetEnumTypeButtonText(TSharedPtr<IPropertyHandle> EnumTypeHandle) const;

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Kept alive so the IStructureDetailsView can edit the struct memory. */
	TSharedPtr<FStructOnScope> StructDefaultValueScope;

	/** Held to keep the view and its delegates alive. */
	TSharedPtr<IStructureDetailsView> StructDefaultValueView;
};
