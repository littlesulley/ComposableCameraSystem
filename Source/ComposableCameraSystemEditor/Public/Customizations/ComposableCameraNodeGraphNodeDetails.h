// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Layout/Visibility.h"
#include "Types/SlateEnums.h"
#include "UObject/WeakObjectPtr.h"

class FPropertyEditorModule;
class IDetailLayoutBuilder;
class UComposableCameraNodeGraphNode;

/**
 * IDetailCustomization for UComposableCameraNodeGraphNode - the EdGraphNode
 * that wraps a single UComposableCameraCameraNodeBase inside the camera type
 * asset's visual node graph.
 *
 * WHAT IT DOES:
 * Replaces the raw auto-generated property grid for the GraphNode with a
 * single unified "Properties" category. Each NodeTemplate UPROPERTY is
 * surfaced via AddExternalObjectProperty so UE's native typed property
 * editors handle every type natively (spinboxes, vector editors, object
 * pickers, struct editors, etc.). For UPROPERTYs that correspond to a
 * declared input pin (matched by name), the row is extended with an
 * "As Pin" checkbox and an "[Exposed]" chip. Pins that have no matching
 * UPROPERTY fall back to a string-based custom row.
 *
 * WHY THE CUSTOMIZATION TARGETS THE GRAPHNODE (NOT THE NODETEMPLATE):
 * Per-instance pin overrides (FComposableCameraPinOverride) live on the
 * GraphNode via its transient RuntimePinOverrides cache (rehydrated on load
 * from the type asset's parallel NodePinOverrides array). To edit them, the
 * Details view has to be pointed at the GraphNode so the customization's
 * IDetailLayoutBuilder::GetObjectsBeingCustomized returns the GraphNode. The
 * toolkit's selection handler is responsible for pushing the GraphNode to the
 * details view when a graph node is selected (see
 * ComposableCameraTypeAssetEditorToolkit::ApplyPendingSelectionToDetails).
 *
 * LIFECYCLE:
 * One instance is created per details-panel layout pass via MakeInstance.
 * All widget callbacks capture `this` as a raw pointer - safe because Slate
 * destroys the widgets together with the IDetailCustomization instance that
 * built them.
 *
 * RELATED:
 * - FComposableCameraParameterTableRowCustomization - precedent for the
 * string-based default value edit pattern (runtime parses at activation
 * time via FComposableCameraParameterBlock::ApplyStringValue).
 * - UComposableCameraNodeGraphNode::SetPinAsPin / SetPinDefaultOverride - 
 * the canonical write paths this customization routes edits through.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraNodeGraphNodeDetails: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Resolve the single graph node this customization is editing. Returns
	 * nullptr if the weak pointer has gone stale (which in practice only
	 * happens during asset teardown). */
	UComposableCameraNodeGraphNode* GetGraphNode() const;

	// Pin row callbacks 

	/** Get the current "As Pin" checkbox state for PinName. */
	ECheckBoxState GetAsPinCheckState(FName PinName) const;

	/** Flip the "As Pin" state for PinName. Transactional - the single toggle
	 * may atomically break wires and unexpose parameters via SetPinAsPin. */
	void OnAsPinCheckChanged(ECheckBoxState NewState, FName PinName);

	/** Visibility for the "[Exposed]" chip next to each pin row. */
	EVisibility GetExposedChipVisibility(FName PinName) const;

	// String-based fallback for pins without a matching UPROPERTY 

	/** Read the effective default-value string for PinName, falling back to
	 * the class-level FComposableCameraNodePinDeclaration::DefaultValueString
	 * passed as ClassDefault. Used only for the string-fallback pin rows
	 * (pins that have no matching UPROPERTY on the NodeTemplate). */
	FText GetPinDefaultValueText(FName PinName, FString ClassDefault) const;

	/** Commit a new default-value string for PinName. The raw string is stored
	 * and the runtime parses it once at activation time, matching the
	 * ParameterTableRow convention. Used only for the string-fallback rows. */
	void OnPinDefaultValueCommitted(const FText& NewText, ETextCommit::Type CommitType, FName PinName);

	/** Weak back-reference to the GraphNode resolved from
	 * IDetailLayoutBuilder::GetObjectsBeingCustomized during CustomizeDetails. */
	TWeakObjectPtr<UComposableCameraNodeGraphNode> WeakGraphNode;
};
