// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "K2Node_ActivateComposableCamera.generated.h"

class UComposableCameraTypeAsset;
class UToolMenu;
class UGraphNodeContextMenuContext;
class FCompilerResultsLog;
struct FComposableCameraExposedParameter;
struct FPropertyChangedEvent;

/**
 * Custom K2 Node for activating a camera from a Camera Type Asset with dynamic Blueprint pins.
 *
 * Pin model - opt-in override:
 *
 * The node does NOT automatically show every exposed parameter / exposed variable
 * from the selected Camera Type Asset. Instead, the author opts in on a per-name
 * basis via the node's right-click "Add Override Pin" submenu. Names that are not
 * opted in simply don't appear on the node; at runtime the camera uses the asset's
 * authored default (the node pin's effective default for exposed parameters,
 * InitialValueString for exposed variables).
 *
 * Required exposed parameters (bRequired == true on the type asset) are an
 * exception: they are ALWAYS shown on the node and cannot be removed, because the
 * runtime's ApplyParameterBlock treats a missing required parameter as a fatal
 * activation error. Required parameters are not tracked in UserOverrideNames - 
 * they are force-created from the asset on every reconstruction.
 *
 * UserOverrideNames (a serialized TArray<FName>) is the node's internal memory of
 * which optional names the author has added. The author never edits this field
 * directly; it is mutated only through the "Add / Remove Override Pin" context
 * menu actions.
 *
 * ExpandNode behavior is unchanged: for each pin the author opted into (plus required
 * parameters), the expansion emits a SetParameterBlockValue call keyed by name, and
 * the final ActivateComposableCameraFromTypeAsset call handles the activation.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_ActivateComposableCamera: public UK2Node
{
	GENERATED_BODY()

public:
	// UObject Interface 
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

	// UEdGraphNode Interface 
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

	// UK2Node Interface 
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual bool IsNodePure() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }

public:
	/** Get the camera type asset this node is configured for. */
	UComposableCameraTypeAsset* GetCameraTypeAsset() const;

private:
	// Pin Management 

	/** Called when the CameraTypeAsset pin's default value changes. Reconstructs dynamic pins. */
	void OnCameraTypeAssetChanged();

	/** Remove all previously created dynamic parameter pins. */
	void RemoveDynamicParameterPins();

	/** Create dynamic pins from the type asset's exposed parameters and variables.
	 * Required parameters are always created; optional parameters and exposed
	 * variables are created only if their name is present in UserOverrideNames. */
	void CreateDynamicParameterPins();

	// The EComposableCameraPinType -> FEdGraphPinType conversion lives in
	// ComposableCameraEdGraphPinTypeUtils so it can be shared with the camera
	// type asset's own visual graph node. See
	// ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType.

	// Override Set Management 

	/** M1 migration: populate UserOverrideNames with all non-required exposed
	 * parameters and all exposed variables from the cached type asset, so that
	 * legacy nodes (which previously auto-created pins for every exposed name)
	 * retain their pin layout after load. */
	void InitializeUserOverridesFromCachedAsset();

	/** Return true if the given name refers to a required exposed parameter on
	 * the cached type asset. Required parameters are force-created on every
	 * reconstruction and cannot be removed via the context menu. */
	bool IsNameRequiredParameter(FName Name) const;

	/** Return true if the given name is present in the cached type asset's
	 * ExposedParameters or ExposedVariables arrays. */
	bool IsNameInCachedAsset(FName Name) const;

	/** Context menu action: add a name to UserOverrideNames and reconstruct. */
	void AddOverridePin(FName Name);

	/** Context menu action: remove a name from UserOverrideNames and reconstruct. */
	void RemoveOverridePin(FName Name);

	/** Context menu action: remove any UserOverrideNames entries that no longer
	 * correspond to a name in the cached type asset. */
	void CleanUpOrphanOverrides();

	// Asset Change Notification 

	/** Subscribe to FCoreUObjectDelegates::OnObjectPropertyChanged so the node
	 * can refresh its pins when the bound type asset is edited in the asset
	 * editor (parameter/variable added, renamed, or removed). */
	void SubscribeToAssetChangeDelegate();

	/** Unsubscribe from the global property-change delegate. */
	void UnsubscribeFromAssetChangeDelegate();

	/** Delegate handler: if the edited object is our CachedTypeAsset, reconstruct
	 * the node so dynamic pins reflect the new asset state. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	// ExpandNode Helpers 

	/** Create an intermediate MakeLiteral node for an unconnected dynamic pin's default value. */
	UK2Node_CallFunction* MakeLiteralValueForPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
		UEdGraphPin* SourceValuePin);

	static UK2Node_CallFunction* CreateMakeLiteralNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
		UK2Node* SourceNode, UClass* FunctionLibraryClass,
		const TCHAR* FunctionName, UEdGraphPin* SourceValuePin);

private:
	/** The camera type asset this node is configured for. Stored for serialization/reconstruction. */
	UPROPERTY()
	TObjectPtr<UComposableCameraTypeAsset> CachedTypeAsset;

	/** Names of dynamically created parameter pins, in pin creation order.
	 * Used as a live cache of "which pins currently exist on this node" for
	 * RemoveDynamicParameterPins and the ExpandNode per-pin iteration. This
	 * includes BOTH required parameters (force-created) and user-opted entries
	 * from UserOverrideNames - it is a runtime cache, not the author intent. */
	UPROPERTY()
	TArray<FName> DynamicParameterPinNames;

	/** Author-opted override set: names of non-required exposed parameters and
	 * exposed variables that the user has explicitly added as override pins via
	 * the "Add Override Pin" right-click menu. Required parameters are NEVER
	 * stored here (they're always force-created). Orphan entries (names no
	 * longer present in the type asset) are preserved across reconstruction so
	 * the user doesn't lose their choices if the asset is in the middle of an
	 * edit; they can be removed explicitly via "Clean Up Orphan Overrides". */
	UPROPERTY()
	TArray<FName> UserOverrideNames;

	/** M1 migration flag. Legacy K2 nodes (saved before the opt-in override
	 * model) need to populate UserOverrideNames with the full set of non-required
	 * exposed names from their asset on first load, to preserve the old behavior
	 * of "every exposed name appears as an advanced pin". New nodes set this to
	 * true in PostPlacedNewNode so they don't get mistaken for legacy data. */
	UPROPERTY()
	bool bUserOverridesInitialized = false;

	/** Handle for the OnObjectPropertyChanged subscription used to auto-refresh
	 * this node when the bound type asset is edited. Transient - resolved in
	 * PostLoad/PostPlacedNewNode, cleared in BeginDestroy. */
	FDelegateHandle ObjectPropertyChangedHandle;

	/** Re-entrancy guard for pin reconstruction. Set while
	 * ReallocatePinsDuringReconstruction is running so that transient pin
	 * notifications fired by the engine's pin-rewire phase
	 * (PinDefaultValueChanged on a freshly-created, not-yet-initialised
	 * CameraTypeAsset pin) or external property-change broadcasts
	 * (FCoreUObjectDelegates::OnObjectPropertyChanged firing during asset load /
	 * save) cannot kick off a nested ReconstructNode that would wipe
	 * UserOverrideNames mid-operation. Transient - never serialised. */
	bool bIsReconstructing = false;

public:
	// Well-Known Pin Names 
	static const FName PN_PlayerIndex;
	static const FName PN_CameraTypeAsset;
	static const FName PN_ContextName;
	static const FName PN_TransitionOverride;
	static const FName PN_ActivationParams;
	static const FName PN_ReturnValue;
};
