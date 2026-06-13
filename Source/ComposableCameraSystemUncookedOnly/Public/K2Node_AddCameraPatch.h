// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "K2Node_AddCameraPatch.generated.h"

class UComposableCameraPatchTypeAsset;
class UToolMenu;
class UGraphNodeContextMenuContext;
class FCompilerResultsLog;
struct FComposableCameraExposedParameter;
struct FPropertyChangedEvent;

/**
 * Custom K2 Node for adding a Camera Patch from a Patch Type Asset with dynamic
 * Blueprint pins. Mirrors UK2Node_ActivateComposableCamera in pin model and
 * compile-time expansion - the only structural differences are the asset class
 * (UComposableCameraPatchTypeAsset), the static input set (PlayerCameraManager
 * + Params struct), and the call target
 * (UComposableCameraBlueprintLibrary::AddCameraPatch).
 *
 * Pin model - opt-in override:
 *
 * Selecting a Patch asset does NOT automatically surface every exposed
 * parameter / exposed variable as a pin. The author opts in per-name via the
 * right-click "Add Override Pin" submenu. Names that are not opted in simply
 * don't appear on the node; at runtime the Patch evaluator uses the asset's
 * authored default (the node pin's effective default for exposed parameters,
 * InitialValueString for exposed variables).
 *
 * Required exposed parameters (bRequired == true on the Patch asset) are an
 * exception: they are ALWAYS shown on the node and cannot be removed, because
 * the runtime's ApplyParameterBlock treats a missing required parameter as a
 * fatal activation error. Required parameters are not tracked in
 * UserOverrideNames - they are force-created from the asset on every
 * reconstruction.
 *
 * ExpandNode behavior: for each pin the author opted into (plus required
 * parameters), the expansion emits a SetParameterBlockValue call keyed by name,
 * routes the temp parameter block into AddCameraPatch's Parameters argument,
 * and the AddCameraPatch call returns the patch handle.
 *
 * No legacy "show everything" migration is needed - this node is introduced
 * after the opt-in override model, so freshly placed nodes start with an
 * empty UserOverrideNames and stay that way unless the author explicitly adds
 * overrides.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_AddCameraPatch: public UK2Node
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
	/** Get the Patch type asset this node is configured for. */
	UComposableCameraPatchTypeAsset* GetPatchTypeAsset() const;

private:
	// Pin Management 

	/** Called when the PatchAsset pin's default value changes. Reconstructs dynamic pins. */
	void OnPatchAssetChanged();

	/** Remove all previously created dynamic parameter pins. */
	void RemoveDynamicParameterPins();

	/** Create dynamic pins from the Patch asset's exposed parameters and variables.
	 * Required parameters are always created; optional parameters and exposed
	 * variables are created only if their name is present in UserOverrideNames. */
	void CreateDynamicParameterPins();

	// The EComposableCameraPinType -> FEdGraphPinType conversion lives in
	// ComposableCameraEdGraphPinTypeUtils so it can be shared with the camera
	// type asset's own visual graph node.

	// Override Set Management 

	/** Return true if the given name refers to a required exposed parameter on
	 * the cached patch asset. Required parameters are force-created on every
	 * reconstruction and cannot be removed via the context menu. */
	bool IsNameRequiredParameter(FName Name) const;

	/** Return true if the given name is present in the cached patch asset's
	 * ExposedParameters or ExposedVariables arrays. */
	bool IsNameInCachedAsset(FName Name) const;

	/** Context menu action: add a name to UserOverrideNames and reconstruct. */
	void AddOverridePin(FName Name);

	/** Context menu action: remove a name from UserOverrideNames and reconstruct. */
	void RemoveOverridePin(FName Name);

	/** Context menu action: remove any UserOverrideNames entries that no longer
	 * correspond to a name in the cached patch asset. */
	void CleanUpOrphanOverrides();

	// Asset Change Notification 

	/** Subscribe to FCoreUObjectDelegates::OnObjectPropertyChanged so the node
	 * can refresh its pins when the bound patch asset is edited. */
	void SubscribeToAssetChangeDelegate();

	/** Unsubscribe from the global property-change delegate. */
	void UnsubscribeFromAssetChangeDelegate();

	/** Delegate handler: if the edited object is our CachedPatchAsset, reconstruct
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
	/** The Patch type asset this node is configured for. Stored for serialization/reconstruction. */
	UPROPERTY()
	TObjectPtr<UComposableCameraPatchTypeAsset> CachedPatchAsset;

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
	 * stored here (they're always force-created). Orphan entries are auto-cleaned
	 * by OnPatchAssetChanged / HandleObjectPropertyChanged when the asset
	 * changes; CleanUpOrphanOverrides is a backstop for the rare case where
	 * neither handler fires. */
	UPROPERTY()
	TArray<FName> UserOverrideNames;

	/** Handle for the OnObjectPropertyChanged subscription used to auto-refresh
	 * this node when the bound patch asset is edited. Transient - resolved in
	 * PostLoad/PostPlacedNewNode, cleared in BeginDestroy. */
	FDelegateHandle ObjectPropertyChangedHandle;

	/** Re-entrancy guard for pin reconstruction. Set while
	 * ReallocatePinsDuringReconstruction is running so that transient pin
	 * notifications fired by the engine's pin-rewire phase
	 * (PinDefaultValueChanged on a freshly-created, not-yet-initialised
	 * PatchAsset pin) or external property-change broadcasts
	 * (FCoreUObjectDelegates::OnObjectPropertyChanged firing during asset load /
	 * save) cannot kick off a nested ReconstructNode that would wipe
	 * UserOverrideNames mid-operation. Transient - never serialised. */
	bool bIsReconstructing = false;

public:
	// Well-Known Pin Names 
	// Note: the Parameters argument on the AddCameraPatch BP library function is
	// not surfaced as a static pin on the K2 node - it's built from dynamic
	// per-name pins at ExpandNode time. The literal "Parameters" string is used
	// only when looking up the corresponding pin on the intermediate CallFunction
	// node during expansion, so it doesn't need a constant here.
	static const FName PN_PlayerIndex;
	static const FName PN_PatchAsset;
	static const FName PN_ContextName;
	static const FName PN_Params;
	static const FName PN_ReturnValue;
};
