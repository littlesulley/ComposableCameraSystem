// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Types/SlateEnums.h"
#include "UObject/WeakObjectPtr.h"

class FPropertyEditorModule;
class IDetailChildrenBuilder;
class IPropertyHandle;
class IPropertyUtilities;
class UClass;
class UComposableCameraModifierBase;
class UComposableCameraNodeModifierDataAsset;

/**
 * Details customization for the Modifier data asset.
 *
 * Every array element is normalized to a base wrapper. Its first row selects
 * generic Node Type mode or Custom Modifier Class mode, then this customization
 * renders only the fields owned by the selected branch.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraModifierDetails final
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	void GenerateModifierElement(
		TSharedRef<IPropertyHandle> ElementHandle,
		int32 ArrayIndex,
		IDetailChildrenBuilder& ChildrenBuilder);
	void AddGenericModifierRows(
		UComposableCameraModifierBase* Modifier,
		int32 ArrayIndex,
		IDetailChildrenBuilder& ChildrenBuilder) const;
	void AddCustomModifierRows(
		UComposableCameraModifierBase* Modifier,
		int32 ArrayIndex,
		IDetailChildrenBuilder& ChildrenBuilder) const;

	UComposableCameraModifierBase* EnsureWrapperForElement(
		const TSharedRef<IPropertyHandle>& ElementHandle,
		int32 ArrayIndex) const;

	static FText GetModeSummary(TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier);
	static ECheckBoxState GetModeCheckState(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier);
	static void OnModeCheckStateChanged(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
		TWeakPtr<IPropertyUtilities> WeakPropertyUtilities,
		ECheckBoxState NewState);

	static ECheckBoxState GetOverrideCheckState(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
		FName PropertyName);
	static void OnOverrideCheckStateChanged(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
		ECheckBoxState NewState,
		FName PropertyName);
	static void OnNodeClassSelected(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
		TWeakPtr<IPropertyUtilities> WeakPropertyUtilities,
		const UClass* SelectedClass);
	static void OnCustomModifierClassSelected(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
		TWeakPtr<IPropertyUtilities> WeakPropertyUtilities,
		const UClass* SelectedClass);
	static void OnCustomTargetNodeClassSelected(
		TWeakObjectPtr<UComposableCameraModifierBase> WeakCustomModifier,
		const UClass* SelectedClass);
	static void RefreshDetails(TWeakPtr<IPropertyUtilities> WeakPropertyUtilities);

	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;
	TWeakObjectPtr<UComposableCameraNodeModifierDataAsset> ModifierAsset;
};
