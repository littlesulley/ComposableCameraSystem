// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "SGraphPin.h"

class SBox;
class SSearchableComboBox;
class UDataTable;
class UK2Node_ActivateComposableCameraFromDataTable;

/**
 * RowName pin widget for UK2Node_ActivateComposableCameraFromDataTable.
 *
 * Presents a searchable combo box populated from the literal DataTable
 * on the node's sibling DataTable pin. Subscribes to the K2 node's
 * OnDataTablePinChanged delegate so the combo options refresh live
 * whenever the DataTable literal changes - no ReconstructNode() churn
 * or save/reopen cycle required.
 *
 * When the DataTable pin is linked or unset, the widget degrades to a
 * plain editable text box so the author can still type a row name that
 * will be resolved at runtime.
 */
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API SGraphPinComposableCameraRowName: public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinComposableCameraRowName) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);
	virtual ~SGraphPinComposableCameraRowName();

protected:
	// SGraphPin 
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	// Widget Construction 

	/** Build the inner combo-or-textbox widget that lives inside the stable
	 * outer DefaultValueContainer. Re-called whenever the DataTable pin
	 * changes state between "resolved" and "unresolved". */
	TSharedRef<SWidget> BuildInnerDefaultValueWidget();

	// Owning K2 Node 

	UK2Node_ActivateComposableCameraFromDataTable* GetOwningActivateNode() const;

	// Data Resolution 

	/** Resolve the DataTable literal via the owning K2 node, if any. */
	UDataTable* ResolveLiteralDataTable() const;

	/** Rebuild RowOptions from the currently resolved DataTable. */
	void RefreshRowOptions();

	/** True when we resolved a valid DataTable and have real row options
	 * (more than just the "None" sentinel). */
	bool HasResolvedDataTable() const;

	// Combo Box Callbacks 

	TSharedRef<SWidget> OnGenerateRowOptionWidget(TSharedPtr<FString> InOption);
	void OnRowOptionSelected(TSharedPtr<FString> InOption, ESelectInfo::Type InSelectInfo);

	// Text Fallback Callback 

	void OnFallbackTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	// Display Helpers 

	FText GetCurrentRowNameText() const;
	FText GetWidgetTooltip() const;

	// Live Refresh 

	/** Bound to UK2Node_ActivateComposableCameraFromDataTable::OnDataTablePinChanged.
	 * Re-runs RefreshRowOptions and forces the container to rebuild the
	 * combo so the new option set is visible immediately. */
	void HandleDataTablePinChanged();

	/** Subscribe HandleDataTablePinChanged to the owning K2 node's delegate. */
	void BindToOwningNodeDelegate();

	/** Unsubscribe on destruct / when the widget is torn down. */
	void UnbindFromOwningNodeDelegate();

private:
	/** Current row options shown in the combo. Always has a "None"
	 * sentinel as element 0. */
	TArray<TSharedPtr<FString>> RowOptions;

	/** Stable outer container that SGraphPin caches as this pin's default
	 * value widget. We swap its inner content on delegate fire to switch
	 * between combo box and text-box fallback modes - SGraphPin has no
	 * public API to rebuild the default value widget, so owning the
	 * outer container ourselves is the only way to replace the inside. */
	TSharedPtr<SBox> DefaultValueContainer;

	/** Strong ref so we can trigger a refresh on live changes. */
	TSharedPtr<SSearchableComboBox> RowCombo;

	/** Weak ref to the K2 node we're subscribed to, used to safely
	 * unsubscribe during teardown without dangling ptrs. */
	TWeakObjectPtr<UK2Node_ActivateComposableCameraFromDataTable> BoundNode;

	/** Handle for the delegate subscription on BoundNode. */
	FDelegateHandle DelegateHandle;
};
