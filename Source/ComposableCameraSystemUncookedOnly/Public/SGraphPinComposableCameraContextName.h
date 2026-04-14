// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"

class SBox;
class SSearchableComboBox;

/**
 * ContextName pin widget for UK2Node_ActivateComposableCamera.
 *
 * Presents a searchable combo box populated from
 * UComposableCameraProjectSettings::GetContextNames(). Unlike the RowName
 * pin widget, no dynamic dependency on another pin exists — the option list
 * is rebuilt from project settings each time the widget is constructed or
 * the combo is opened.
 */
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API SGraphPinComposableCameraContextName
	: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinComposableCameraContextName) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	// ─── SGraphPin ────────────────────────────────────────────────────────
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	// ─── Data Resolution ──────────────────────────────────────────────────

	/** Rebuild ContextOptions from UComposableCameraProjectSettings. */
	void RefreshContextOptions();

	// ─── Combo Box Callbacks ──────────────────────────────────────────────

	TSharedRef<SWidget> OnGenerateOptionWidget(TSharedPtr<FString> InOption);
	void OnOptionSelected(TSharedPtr<FString> InOption, ESelectInfo::Type InSelectInfo);

	// ─── Display Helpers ──────────────────────────────────────────────────

	FText GetCurrentContextNameText() const;

private:
	/** Current context options shown in the combo. Always has a "None"
	 *  sentinel as element 0. */
	TArray<TSharedPtr<FString>> ContextOptions;

	/** Strong ref so we can trigger a refresh on combo open. */
	TSharedPtr<SSearchableComboBox> ContextCombo;
};
