// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES() \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(bool, Boolean) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(int32, Integer32) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(float, Float) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(double, Double) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FVector2f, Vector2f) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FVector2d, Vector2d) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FVector3f, Vector3f) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FVector3d, Vector3d) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FVector4f, Vector4f) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FVector4d, Vector4d) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FRotator3f, Rotator3f) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FRotator3d, Rotator3d) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FTransform3f, Transform3f) \
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(FTransform3d, Transform3d)

/**
 * Details customization for context parameters.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraContextParameterDetailsCustomization
	: public IPropertyTypeCustomization
	, public FTickableEditorObject
{
public:
	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FComposableCameraContextParameterDetailsCustomization, STATGROUP_Tickables); }

protected:
	virtual bool HasNonUserOverride(void* InRawData) = 0;
	virtual void SetParameterVariable(void* InRawData, UComposableCameraVariable* InVariable) = 0; 

private:
	enum class EComposableCameraVariableValue
	{
		NotSet,
		Set,
		MultipleSet,
		Invalid
	};

	struct FComposableCameraVariableInfo
	{
		UComposableCameraVariable* CommonVariable = nullptr;
		EComposableCameraVariableValue VariableValue = EComposableCameraVariableValue::NotSet;
		FText InfoText;
		FText ErrorText;
	};

	void UpdateVariableInfo();

	TSharedRef<SWidget> BuildCameraVariableBrowser();

	bool IsValueEditorEnabled() const;
	FText GetCameraVariableBrowserToolTip() const;

	FText GetVariableInfoText() const;
	EVisibility GetVariableInfoTextVisibility() const;
	FOptionalSize GetVariableInfoTextMaxWidth() const;

	FText GetVariableErrorText() const;
	EVisibility GetVariableErrorTextVisibility() const;
	FOptionalSize GetVariableErrorTextMaxWidth() const;

	bool CanGoToVariable() const;
	void OnGoToVariable();

	bool CanClearVariable() const;
	void OnClearVariable();

	void OnSetVariable(UComposableCameraVariable* InVariable);

	bool IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> InPropertyHandle) const;
	void OnResetToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle);

protected:
	UClass* VariableClass = nullptr;

	FComposableCameraVariableInfo VariableInfo;

	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyHandle> ValueProperty;
	TSharedPtr<IPropertyHandle> VariableProperty;

	TSharedPtr<SHorizontalBox> LayoutBox;
	TSharedPtr<SComboButton> VariableBrowserButton;
};

#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(ValueType, ValueName) \
class F##ValueName##ComposableCameraContextParameterDetailsCustomization \
	: public FComposableCameraContextParameterDetailsCustomization \
{ \
protected: \
virtual bool HasNonUserOverride(void* InRawData) override; \
virtual void SetParameterVariable(void* InRawData, UComposableCameraVariable* InVariable) override; \
};
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES()
#undef COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE