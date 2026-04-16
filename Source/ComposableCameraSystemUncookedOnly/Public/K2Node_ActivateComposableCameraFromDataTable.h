// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "K2Node_ActivateComposableCameraFromDataTable.generated.h"

class UDataTable;
class UScriptStruct;
class UComposableCameraTypeAsset;
class UToolMenu;
class UGraphNodeContextMenuContext;
class FCompilerResultsLog;
struct FComposableCameraExposedParameter;
struct FPropertyChangedEvent;

/**
 * Custom K2 Node for activating a camera from a DataTable row, with
 * per-call-site parameter override pins.
 *
 * This node replaces the previous UK2Node_CallFunction wrapper and now
 * inherits from UK2Node directly, giving it full control over pin layout
 * and ExpandNode compilation.
 *
 * Static pins:
 *   - Exec In / Exec Out
 *   - Player Index (int32, default 0)
 *   - DataTable (UDataTable* with filtered asset picker)
 *   - RowName (FName with live-refreshing combo)
 *   - Return Value (AComposableCameraCameraBase*)
 *
 * Dynamic pins:
 *   The node resolves the CameraType from the literal DataTable + RowName
 *   and then offers the same "Add Override Pin" right-click menu as the
 *   sibling UK2Node_ActivateComposableCamera. Override pin values take
 *   precedence over the row's string-map values at runtime.
 *
 * Pin model — opt-in override:
 *   Same as the sibling node: required exposed parameters are always shown,
 *   optional parameters and exposed variables are added via right-click
 *   "Add Override Pin". Names not overridden use the row value (or the
 *   asset default if the row omits them).
 *
 * ExpandNode:
 *   1. Build a FComposableCameraParameterBlock from dynamic override pins
 *   2. Call ActivateComposableCameraFromDataTable(World, PlayerIndex,
 *      DataTable, RowName, OverrideParameterBlock)
 *   The runtime function builds base params from the row, then merges
 *   the override block on top.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_ActivateComposableCameraFromDataTable
	: public UK2Node
{
	GENERATED_BODY()

public:
	// ─── UObject Interface ────────────────────────────────────────────────
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

	// ─── UEdGraphNode Interface ───────────────────────────────────────────
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostPlacedNewNode() override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	// ─── UK2Node Interface ────────────────────────────────────────────────
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual bool IsNodePure() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }

public:
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

	/** Get the cached camera type asset resolved from the DataTable row. */
	UComposableCameraTypeAsset* GetCameraTypeAsset() const;

	// ─── Well-Known Pin Names ─────────────────────────────────────────────
	static const FName DataTablePinName;
	static const FName RowNamePinName;
	static const FName PN_PlayerIndex;
	static const FName PN_ReturnValue;

private:
	// ─── DataTable / CameraType Resolution ────────────────────────────────

	/** Resolve the CameraType from the literal DataTable + RowName pins.
	 *  Updates CachedTypeAsset and reconstructs if the asset changed. */
	void ResolveCameraTypeFromDataTable();

	/** If the RowName pin currently holds a name that doesn't exist in the
	 *  newly-resolved DataTable, clear it to None. */
	void ClearRowNameIfInvalidForCurrentDataTable();

	// ─── Pin Management ───────────────────────────────────────────────────

	/** Remove all previously created dynamic parameter pins. */
	void RemoveDynamicParameterPins();

	/** Create dynamic pins from the type asset's exposed parameters and variables. */
	void CreateDynamicParameterPins();

	// ─── Override Set Management ──────────────────────────────────────────

	/** Return true if the given name refers to a required exposed parameter. */
	bool IsNameRequiredParameter(FName Name) const;

	/** Return true if the given name is present in the cached type asset. */
	bool IsNameInCachedAsset(FName Name) const;

	/** Context menu action: add a name to UserOverrideNames and reconstruct. */
	void AddOverridePin(FName Name);

	/** Context menu action: remove a name from UserOverrideNames and reconstruct. */
	void RemoveOverridePin(FName Name);

	/** Remove any UserOverrideNames entries that no longer correspond to
	 *  a name in the cached type asset. */
	void CleanUpOrphanOverrides();

	// ─── Asset Change Notification ────────────────────────────────────────

	void SubscribeToAssetChangeDelegate();
	void UnsubscribeFromAssetChangeDelegate();
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	// ─── ExpandNode Helpers ───────────────────────────────────────────────

	UK2Node_CallFunction* MakeLiteralValueForPin(
		FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
		UEdGraphPin* SourceValuePin);

	static UK2Node_CallFunction* CreateMakeLiteralNode(
		FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
		UK2Node* SourceNode, UClass* FunctionLibraryClass,
		const TCHAR* FunctionName, UEdGraphPin* SourceValuePin);

private:
	/** Camera type asset resolved from the DataTable row. Null if the
	 *  DataTable or RowName pins are linked, unset, or the row has no
	 *  valid CameraType. */
	UPROPERTY()
	TObjectPtr<UComposableCameraTypeAsset> CachedTypeAsset;

	/** Names of dynamically created parameter pins, in pin creation order. */
	UPROPERTY()
	TArray<FName> DynamicParameterPinNames;

	/** Author-opted override set: non-required exposed parameters and
	 *  exposed variables the user has explicitly added via "Add Override Pin". */
	UPROPERTY()
	TArray<FName> UserOverrideNames;

	/** Handle for OnObjectPropertyChanged subscription. */
	FDelegateHandle ObjectPropertyChangedHandle;

	/** Delegate broadcast when the DataTable pin changes. */
	FOnDataTablePinChanged OnDataTablePinChangedDelegate;
};
