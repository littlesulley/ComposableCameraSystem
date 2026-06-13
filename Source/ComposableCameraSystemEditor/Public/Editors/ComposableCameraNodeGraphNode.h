// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editors/ComposableCameraGraphNodeBase.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraNodeGraphNode.generated.h"

class UComposableCameraCameraNodeBase;
struct FComposableCameraNodePinDeclaration;

/**
 * EdGraphNode subclass representing a single camera node in the visual graph.
 * Auto-generates input/output pins from the underlying node template's pin declarations.
 *
 * Inherits the canonical PN_ExecIn / PN_ExecOut name constants and the
 * CreateExecInPin / CreateExecOutPin helpers from UComposableCameraGraphNodeBase.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraNodeGraphNode: public UComposableCameraGraphNodeBase
{
	GENERATED_BODY()

public:
	/** The node template this graph node represents (owned by the type asset).
	 *
	 * MUST be Transient. The base class UComposableCameraCameraNodeBase is
	 * declared with `UCLASS(DefaultToInstanced, EditInlineNew, ...)`, which
	 * causes UHT to implicitly stamp CPF_PersistentInstance |
	 * CPF_InstancedReference | CPF_ExportObject onto every TObjectPtr /
	 * raw pointer property that references the class - regardless of whether
	 * the property author wrote `Instanced` explicitly.
	 *
	 * That promotion is fine when the containing property's Outer matches the
	 * subobject's Outer (as with UComposableCameraTypeAsset::NodeTemplates,
	 * whose entries are outered to the TypeAsset). It is actively harmful
	 * here: a UComposableCameraNodeGraphNode is outered to the EdGraph
	 * (outered to the TypeAsset), but the NodeTemplate it points at is
	 * outered directly to the TypeAsset. On save, the GraphNode's serializer
	 * treats NodeTemplate as an owned inline subobject and tries to export
	 * it under the GraphNode - colliding with the TypeAsset's NodeTemplates
	 * array export of the same object. The observed symptom is that on
	 * reopen, OwningTypeAsset->NodeTemplates comes back empty and
	 * RebuildFromTypeAsset then rebuilds a blank graph - i.e. "all content
	 * disappears after save".
	 *
	 * Marking this field Transient stops the graph from serializing the
	 * reference at all. It's safe because RebuildFromTypeAsset repopulates
	 * NodeTemplate on every editor open from
	 * OwningTypeAsset->NodeTemplates[NodeIndex] - NodeIndex is the durable
	 * identity, NodeTemplate is just a cached fast-access pointer. See
	 * UComposableCameraNodeGraph::RebuildFromTypeAsset. */
	UPROPERTY(Transient)
	TObjectPtr<UComposableCameraCameraNodeBase> NodeTemplate;

	/** Index of this node in the type asset's NodeTemplates array.
	 * This is the durable identity for the GraphNode - NodeTemplate above
	 * is recovered from this index on every editor open. */
	UPROPERTY()
	int32 NodeIndex = INDEX_NONE;

	/** Cached copy of this instance's pin overrides, loaded on rebuild from
	 * OwningTypeAsset->NodePinOverrides[NodeIndex] and pushed back on sync.
	 *
	 * Transient for the same reason NodeTemplate is: the type asset is the
	 * source of truth and the round-trip copies this field back and forth
	 * in the Sync/Rebuild phases. Keeping a live copy on the graph node
	 * itself means AllocateDefaultPins / the Details customization / the
	 * pin factory can read overrides through a plain pointer lookup
	 * without re-walking the type asset every time.
	 *
	 * Accessors below (FindPinOverride / GetEffectivePinDefault /
	 * GetEffectivePinAsPin / SetPinDefaultOverride / SetPinAsPin) are the
	 * canonical read and write paths - no other code should touch this
	 * field directly. */
	UPROPERTY(Transient)
	TArray<FComposableCameraPinOverride> RuntimePinOverrides;

	// Copy/Paste Transport 
	//
	// NodeTemplate and RuntimePinOverrides are Transient (see comments above),
	// which means they don't survive FEdGraphUtilities::ExportNodesToText -> 
	// ImportNodesFromText round-trips (clipboard copy/paste). The fields
	// below serve as non-Transient cargo that PrepareForCopying populates
	// right before export and PostPasteNode consumes after import. After the
	// paste fixup they are cleared to avoid leaving orphan UObjects around.
	//
	// CopyPasteNodeTemplate is declared Instanced so the contained UObject
	// is exported inline during copy. On import it is deserialized under the
	// pasted graph node; PostPasteNode then reparents it to the TypeAsset.
	// This is safe despite the DefaultToInstanced cross-outer warning on
	// NodeTemplate because CopyPasteNodeTemplate only coexists with
	// NodeTemplate briefly (between PrepareForCopying and the next graph
	// rebuild), and neither the EdGraph nor the graph node are ever saved to
	// disk (the graph is Transient on the TypeAsset).

	/** Full clone of the source NodeTemplate, populated by PrepareForCopying
	 * and consumed by PostPasteNode. Non-Transient + Instanced so it
	 * survives the clipboard round-trip. Cleared after paste. */
	UPROPERTY(Instanced)
	TObjectPtr<UComposableCameraCameraNodeBase> CopyPasteNodeTemplate;

	/** Snapshot of RuntimePinOverrides, populated by PrepareForCopying and
	 * consumed by PostPasteNode. Non-Transient so it survives the clipboard
	 * round-trip. Cleared after paste. */
	UPROPERTY()
	TArray<FComposableCameraPinOverride> CopyPastePinOverrides;

	// PN_ExecIn / PN_ExecOut are inherited from UComposableCameraGraphNodeBase.

	// UEdGraphNode Interface 

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PrepareForCopying() override;
	virtual void PostPasteNode() override;

	/** After an undo or redo has restored RuntimePinOverrides (and the other
	 * side of any broken wires), rebuild our Pins array so the live UEdGraphPin
	 * set matches the restored override state.
	 *
	 * Why this is necessary: toggling bAsPin off on a wired pin follows this
	 * path inside one FScopedTransaction - 
	 * 1. Modify() captures RuntimePinOverrides + Pins + wire LinkedTo on
	 * both sides.
	 * 2. SetPinAsPin flips the override and BreakAllPinLinks's the live pin.
	 * 3. ReconstructPins() nukes the old pin and calls MarkAsGarbage() on it
	 * before the transaction closes.
	 * Ctrl+Z restores the serialized state - RuntimePinOverrides is back to
	 * bAsPin==true and the other side's LinkedTo is back - but the trashed
	 * UEdGraphPin objects the restored Pins array would have pointed at are
	 * in an inconsistent state. Without intervention, the user sees a pin
	 * that vanished and a wire that did not come back.
	 *
	 * The fix is to treat undo / redo like any other override-change event:
	 * walk the declarations again, rebuild pins from scratch with the restored
	 * RuntimePinOverrides, and let ReconstructPins's built-in wire-matching
	 * logic carry any LinkedTo that the transaction restored onto the fresh
	 * pins via MovePersistentDataFromOldPin. Then notify the graph so the
	 * canvas widget repaints.
	 *
	 * This matches the standard UK2Node / material-graph-node pattern where
	 * PostEditUndo triggers a full node reconstruct. */
	virtual void PostEditUndo() override;

	/** Rebuild pins from the node template's pin declarations. */
	void ReconstructPins();

	/** Check if a specific input pin is exposed as a camera parameter. */
	bool IsInputPinExposed(FName PinName) const;

	/** Mark a pin as exposed (creates the exposed parameter on the type asset). */
	void ExposePinAsParameter(FName PinName);

	/** Remove a pin's exposure (removes the exposed parameter from the type asset). */
	void UnexposePinParameter(FName PinName);

	// Pin Override Accessors 
	//
	// Overrides are stored sparsely: a declared pin without an entry in
	// RuntimePinOverrides inherits (bAsPin = true, DefaultValueString from
	// the class-level FComposableCameraNodePinDeclaration). The Get*
	// helpers encapsulate this fallback so callers never have to remember
	// to check the sparse case. The Set* helpers lazily create an entry
	// on first write.

	/** Find the override entry for PinName, or nullptr if this pin has no
	 * authored override yet. Read-only lookup; callers that want the
	 * effective value should prefer GetEffective*. */
	const FComposableCameraPinOverride* FindPinOverride(FName PinName) const;
	FComposableCameraPinOverride* FindPinOverride(FName PinName);

	/** Return the effective default-value string for PinName, applying the
	 * per-asset override if present and falling back to the C++ declaration
	 * on the node class otherwise. Used by AllocateDefaultPins to seed
	 * UEdGraphPin::DefaultValue and by the Details customization as the
	 * displayed value. */
	FString GetEffectivePinDefault(const FComposableCameraNodePinDeclaration& Declaration) const;

	/** Return the effective bAsPin state for PinName, applying the override
	 * if present and defaulting to true otherwise. Used by AllocateDefaultPins
	 * to decide whether to materialize a UEdGraphPin for the declaration. */
	bool GetEffectivePinAsPin(FName PinName) const;

	/** Write the default-value override for PinName, creating a sparse entry
	 * if one doesn't exist yet. Marks the package dirty. Does NOT rebuild
	 * pins - callers that want the graph to reflect the change should call
	 * ReconstructPins themselves. */
	void SetPinDefaultOverride(FName PinName, const FString& NewDefault);

	/** Toggle the bAsPin state for PinName, creating a sparse entry if needed.
	 * When turning bAsPin off on a pin that is currently wired or exposed as
	 * a camera parameter, this helper also breaks the wire (silent /
	 * transactional) and auto-unexposes the parameter. Does NOT rebuild pins
	 * on its own - callers should ReconstructPins afterward. */
	void SetPinAsPin(FName PinName, bool bNewAsPin);

	// Runtime Debug State (Editor Only) 

	/** Transient debug state pushed by the toolkit's debug ticker during PIE.
	 * Read by SComposableCameraGraphNode to render active-node glow and
	 * live pin value overlays. Reset to defaults when PIE ends. */
	struct FDebugState
	{
		/** True when this node's runtime counterpart was ticked this frame. */
		bool bIsActive = false;

		/** Camera pose after this node executed. */
		FComposableCameraPose PoseAfterNode;

		/** Output pin values as formatted strings (PinName -> display text). */
		TMap<FName, FString> OutputPinDisplayValues;

		void Reset()
		{
			bIsActive = false;
			PoseAfterNode = FComposableCameraPose();
			OutputPinDisplayValues.Reset();
		}
	};

	/** Current debug state. Only meaningful during PIE with a debugged camera. */
	FDebugState DebugState;

private:
	/** Create an EdGraphPin from a pin declaration. The enum-to-FEdGraphPinType
	 * conversion is delegated to ComposableCameraEdGraphPinTypeUtils so the
	 * K2 ActivateComposableCamera node and this graph node share a single
	 * source of truth for pin-type translation. */
	UEdGraphPin* CreatePinFromDeclaration(const FComposableCameraNodePinDeclaration& Declaration);
};
