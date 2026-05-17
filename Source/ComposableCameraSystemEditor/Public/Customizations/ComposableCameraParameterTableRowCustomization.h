// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Nodes/ComposableCameraNodePinTypes.h"

class IPropertyHandle;
class IPropertyUtilities;
class IDetailChildrenBuilder;
class IStructureDetailsView;
class FDetailWidgetRow;
class FPropertyEditorModule;
struct FComposableCameraExposedParameterValues;
struct FComposableCameraParameterTableRow;
class FStructOnScope;
class UComposableCameraTypeAsset;
class UScriptStruct;
class UEnum;

/**
 * Property type customization for FComposableCameraExposedParameterValues - 
 * the sub-struct that wraps the row's parameter map.
 *
 * WHY NOT CUSTOMIZE THE ROW DIRECTLY:
 * The DataTable row editor renders rows through FStructureDetailsView, which
 * does NOT invoke IPropertyTypeCustomization at the ROOT struct level - it
 * only applies customizations to CHILD struct properties. Registering this
 * customization against FComposableCameraParameterTableRow would therefore
 * silently no-op in the row editor. Registering it against the wrapper
 * sub-struct makes it a child property of the row and the details view
 * routes it through us.
 *
 * BEHAVIOR:
 * 1. Walks up via GetParentHandle() to locate the sibling CameraType on
 * the parent row, sync-loads it, and iterates its ExposedParameters.
 * 2. Generates one row per exposed parameter/variable. Each row has a
 * checkbox (override toggle) and a type-appropriate value widget:
 * checkbox/spinner/vector components/transform rows for typed values,
 * inline IStructureDetailsView for Struct types. The widget is
 * disabled when the checkbox is unchecked (using the asset's default).
 * 3. Entries whose keys are not present on the current CameraType are
 * grouped into a collapsed "Orphaned" section so swapping CameraType
 * doesn't silently destroy values. Users can remove them explicitly.
 * 4. When the parent row's CameraType changes, the customization forces a
 * details-panel refresh so the per-parameter widgets rebuild against
 * the new exposed parameter list.
 *
 * The TMap<FName,FString> is the ground truth; this customization is a typed
 * view over it. Values serialized here remain authorable by hand.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraParameterTableRowCustomization: public IPropertyTypeCustomization
{
public:
	~FComposableCameraParameterTableRowCustomization();

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Returns the first raw wrapper pointer, or null if the handle doesn't
	 * point at exactly one instance (multi-select degrades to read-only). */
	FComposableCameraExposedParameterValues* GetWrapperPtr() const;

	/** Returns the first raw row pointer by walking up to the parent handle.
	 * Used to locate the sibling CameraType field at the row level. */
	FComposableCameraParameterTableRow* GetRowPtr() const;

	/** Force-refresh the details view so CustomizeChildren runs again after a
	 * CameraType change on the parent row. */
	void RequestRefresh();

	/** Returns whether ParameterName currently has an override entry in the Values map. */
	bool IsParameterOverridden(FName ParameterName) const;

	/** Toggles the override state. On -> copies default into Values map; Off -> removes from map. */
	void OnOverrideToggled(ECheckBoxState NewState, FName ParameterName, FString DefaultValue);

	// Typed Widget Builders 

	/** Read the current raw string for ParameterName from the wrapper's Values
	 * map. Returns the DefaultValue when no override entry exists. */
	FString GetParameterString(FName ParameterName, const FString& DefaultValue) const;

	/** Write a raw string value for ParameterName into the wrapper's Values map
	 * with full transaction support. */
	void SetParameterString(FName ParameterName, const FString& NewValue);

	/** Build a type-appropriate value widget that reads/writes a named entry in
	 * the wrapper's Values map. Falls back to a text box for unsupported types.
	 * EnumType is consulted only when PinType == Enum; ignored otherwise. */
	TSharedRef<SWidget> BuildTypedValueWidget(FName ParameterName,
		EComposableCameraPinType PinType,
		UScriptStruct* StructType,
		UEnum* EnumType,
		const FString& DefaultValue);

	/** Build a horizontal row of N labelled numeric spinners for multi-component
	 * types (Vector2D/3D/4, Rotator). */
	TSharedRef<SWidget> BuildNumericComponentWidget(FName ParameterName,
		int32 ComponentIndex,
		int32 NumComponents,
		const TCHAR* const* ComponentLabels,
		const TCHAR* Prefix,
		const FString& DefaultValue);

	/** Build Location/Rotation/Scale rows for FTransform values. */
	TSharedRef<SWidget> BuildTransformWidget(FName ParameterName,
		const FString& DefaultValue);

	/** Build an inline IStructureDetailsView for a Struct-typed parameter.
	 * The view reads/writes through the wrapper's Values map. */
	TSharedRef<SWidget> BuildStructValueWidget(FName ParameterName,
		UScriptStruct* InStructType,
		const FString& DefaultValue);

	/** Tooltip describing the expected serialized format for a given pin type.
	 * EnumType is consulted only when PinType == Enum; ignored otherwise. */
	FText GetFormatHint(EComposableCameraPinType PinType, UScriptStruct* StructType, UEnum* EnumType) const;

	/** Unbind the asset-change delegate if currently bound. */
	void UnbindAssetChangeDelegate();

	/** Handle to the FComposableCameraExposedParameterValues wrapper struct
	 * this customization was invoked on. */
	TSharedPtr<IPropertyHandle> WrapperPropertyHandle;

	/** Handle to the parent row struct, resolved via GetParentHandle(). Used
	 * to read the sibling CameraType field. */
	TSharedPtr<IPropertyHandle> RowPropertyHandle;

	/** Handle to the row's CameraType sibling. Used to hook the value-changed
	 * delegate so widgets rebuild when the user picks a different type. */
	TSharedPtr<IPropertyHandle> CameraTypeHandle;

	/** Handle to the wrapper's Values TMap, used for edit-notification brackets. */
	TSharedPtr<IPropertyHandle> ValuesHandle;

	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Delegate handle for FCoreUObjectDelegates::OnObjectPropertyChanged - 
	 * triggers a refresh when the resolved type asset's content changes. */
	FDelegateHandle AssetChangedDelegateHandle;

	/** Weak reference to the currently watched type asset so we can filter
	 * the global OnObjectPropertyChanged broadcast. */
	TWeakObjectPtr<UComposableCameraTypeAsset> WatchedTypeAsset;

	/** Keeps FStructOnScope instances alive for inline struct detail views. */
	TArray<TSharedPtr<FStructOnScope>> StructScopes;

	/** Keeps IStructureDetailsView instances alive for inline struct views. */
	TArray<TSharedPtr<IStructureDetailsView>> StructDetailViews;
};
