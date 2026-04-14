// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ActivateComposableCameraFromDataTable.generated.h"

class UDataTable;
class UScriptStruct;

/**
 * Custom K2 Node for activating a camera from a DataTable row.
 *
 * This node exists purely to give the BP author a better authoring experience
 * than the raw UComposableCameraBlueprintLibrary::ActivateComposableCameraFromDataTable
 * call would provide:
 *
 *  - The DataTable asset picker is filtered so only DataTables whose row
 *    struct is FComposableCameraParameterTableRow are accepted.
 *  - The RowName pin becomes a searchable combo box populated from the
 *    literal DataTable selected on this node, refreshed live (via the
 *    OnDataTablePinChanged delegate) whenever the DataTable pin changes.
 *
 * At compile time this node expands to a plain call to
 * ActivateComposableCameraFromDataTable — it's a pure authoring skin over
 * the existing BP library function. Because the function is marked
 * BlueprintInternalUseOnly, this custom node is the only way to spawn the
 * call from the Blueprint palette.
 *
 * The node inherits from UK2Node_CallFunction and sets its FunctionReference
 * in the constructor, so title, tooltip, and pin layout all come for free
 * from the wrapped function. All we add on top is:
 *   - A menu registration that surfaces this subclass (not the raw function).
 *   - PinDefaultValueChanged / PinConnectionListChanged overrides that fire
 *     our own OnDataTablePinChanged delegate, which the row-name pin widget
 *     subscribes to for live refresh without ReconstructNode() churn.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_ActivateComposableCameraFromDataTable
	: public UK2Node_CallFunction
{
	GENERATED_BODY()

public:
	UK2Node_ActivateComposableCameraFromDataTable(const FObjectInitializer& ObjectInitializer);

	// ─── UK2Node Interface ────────────────────────────────────────────────
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;

	// ─── UEdGraphNode Interface ───────────────────────────────────────────
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	/** Broadcast when the DataTable pin's default value or connection state
	 *  changes. The row-name pin widget subscribes to this so its combo
	 *  options refresh live without a full node reconstruction. */
	DECLARE_MULTICAST_DELEGATE(FOnDataTablePinChanged);
	FOnDataTablePinChanged& GetOnDataTablePinChanged() { return OnDataTablePinChangedDelegate; }

	/** Resolve the DataTable literal currently set on this node's DataTable
	 *  pin. Returns nullptr if the pin is linked, unset, or points at a
	 *  DataTable whose row struct is not FComposableCameraParameterTableRow. */
	UDataTable* ResolveLiteralDataTable() const;

	/** Row struct this K2 node filters its DataTable pin against. Exposed
	 *  as a static so the pin widget can share the exact same filter. */
	static UScriptStruct* GetRequiredRowStruct();

	/** Name of the DataTable input pin on this node. Matches the BP library
	 *  function parameter name so UK2Node_CallFunction builds it automatically. */
	static const FName DataTablePinName;

	/** Name of the RowName input pin on this node. Matches the BP library
	 *  function parameter name so UK2Node_CallFunction builds it automatically. */
	static const FName RowNamePinName;

private:
	/** If the RowName pin currently holds a name that doesn't exist in the
	 *  newly-resolved DataTable, clear it to None. Called from both
	 *  PinDefaultValueChanged and PinConnectionListChanged so any transition
	 *  of the DataTable pin revalidates the row name. This prevents a stale
	 *  row name (e.g. "1" from a previous DataTable) from silently persisting
	 *  when the user swaps to a different DataTable whose row set doesn't
	 *  include that name. */
	void ClearRowNameIfInvalidForCurrentDataTable();

	FOnDataTablePinChanged OnDataTablePinChangedDelegate;
};
