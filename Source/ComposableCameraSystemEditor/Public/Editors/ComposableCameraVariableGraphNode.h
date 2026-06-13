// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraVariableGraphNode.generated.h"

struct FComposableCameraInternalVariable;
class UComposableCameraTypeAsset;

/**
 * EdGraphNode that represents a single internal-variable read (Get) or write (Set)
 * inside the visual camera type graph.
 *
 * Get nodes expose a single output pin whose type matches the variable's declared type.
 * They are pure data conduits - reads happen implicitly whenever the camera node
 * wired to a Get node executes, so Get nodes carry no exec pins.
 *
 * Set nodes expose an input pin for the value plus a pair of exec pins (ExecIn /
 * ExecOut). Writes are explicit: the Set node participates in the exec chain so
 * that the order of writes relative to camera-node evaluation is well-defined.
 * The editor walks the exec chain and records Set-variable steps into
 * UComposableCameraTypeAsset::FullExecChain.
 *
 * These nodes are editor-only; they are persisted on the owning type asset as
 * FComposableCameraVariableNodeRecord entries and round-tripped through
 * UComposableCameraNodeGraph::SyncToTypeAsset / RebuildFromTypeAsset.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraVariableGraphNode: public UEdGraphNode
{
	GENERATED_BODY()

public:
	/**
	 * Stable identity of the internal variable this node reads from / writes to.
	 *
	 * When the user renames a variable in the Details panel, VariableName on the
	 * owning type asset changes but VariableGuid stays the same - so this node
	 * still resolves correctly via FindVariable() (which matches on GUID first).
	 *
	 * May be invalid on legacy data saved before GUID migration; in that case
	 * FindVariable() falls back to matching by VariableName and repairs the
	 * GUID the next time RebuildFromTypeAsset runs.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Variable")
	FGuid VariableGuid;

	/**
	 * Cached name of the internal variable, used as a legacy fallback and as
	 * the display string when the variable no longer exists (shown as "(missing)").
	 * The authoritative identity is VariableGuid.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Variable")
	FName VariableName;

	/** True for Set nodes, false for Get nodes. */
	UPROPERTY(VisibleAnywhere, Category = "Variable")
	bool bIsSetter = false;

	// Cached Variable Metadata (survives copy/paste) 
	//
	// Populated opportunistically whenever FindVariable() succeeds.
	// When the node is pasted into a different graph where the original
	// variable no longer exists, these cached values let the "Create
	// Variable" context-menu action recreate a matching variable without
	// guessing the type.

	/** Cached pin type of the variable. */
	UPROPERTY()
	EComposableCameraPinType CachedVariableType = EComposableCameraPinType::Float;

	/** Cached struct type (only meaningful when CachedVariableType == Struct). */
	UPROPERTY()
	TObjectPtr<UScriptStruct> CachedStructType = nullptr;

	/** Cached enum type (only meaningful when CachedVariableType == Enum). */
	UPROPERTY()
	TObjectPtr<UEnum> CachedEnumType = nullptr;

	/** Whether the source variable lived in ExposedVariables (true) or InternalVariables (false). */
	UPROPERTY()
	bool bCachedIsExposed = false;

	/**
	 * True once CacheVariableMetadata has run at least once (i.e. FindVariable()
	 * succeeded at some point). Used by AllocateDefaultPins to distinguish
	 * "we have real cached type info" from "fields are still at their defaults".
	 * Without this, a Float variable's cache is indistinguishable from the
	 * default-constructed state and falls through to a wildcard pin.
	 */
	UPROPERTY()
	bool bHasValidCachedType = false;

	// Well-Known Pin Names 

	/** Output pin on Get nodes / input pin on Set nodes. */
	static const FName PN_Value;

	/** Exec input pin on Set nodes (absent on Get nodes). */
	static const FName PN_ExecIn;

	/** Exec output pin on Set nodes (absent on Get nodes). */
	static const FName PN_ExecOut;

	// UEdGraphNode Interface 

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PostPasteNode() override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

	// Helpers 

	/** Find the variable declaration on the owning type asset. Returns nullptr if missing. */
	const FComposableCameraInternalVariable* FindVariable() const;

	/** Find the owning camera type asset by walking up through the graph's outer. */
	UComposableCameraTypeAsset* GetOwningTypeAsset() const;

	/** Rebuild pins from the current variable lookup (call after renames / type changes). */
	void ReconstructPins();

	/**
	 * Try to auto-associate this node with an existing variable on the target
	 * type asset by matching VariableName + CachedVariableType. Returns true
	 * if a match was found and the node was rebound.
	 */
	bool TryAutoAssociateWithExistingVariable();

	/**
	 * Create a new variable on the owning type asset from the cached metadata,
	 * then rebind this node to the newly created variable and reconstruct pins.
	 */
	void CreateVariableFromCachedInfo();

	/**
	 * Called from PostPasteNode when a name match with type mismatch is detected
	 * (TryAutoAssociateWithExistingVariable already failed). Shows a modal dialog
	 * offering three resolution strategies:
	 * (1) Adopt existing - rebind to the same-name variable, change pin type
	 * (2) Change existing - modify the existing variable's type to match the pasted node
	 * (3) Rename - create a new variable with a unique suffix (e.g. "Speed_1")
	 * Does nothing if no name conflict exists.
	 */
	void HandleVariableTypeConflictIfAny();

	/**
	 * Create a new variable with a unique suffix appended to VariableName
	 * (e.g. "Speed_1"), then rebind this node to it. Used by the type
	 * conflict dialog's "rename" option.
	 */
	void RenameWithUniqueSuffix();

	// Runtime Debug State 

	/** Transient debug state pushed by the toolkit's debug ticker during PIE. */
	struct FDebugState
	{
		bool bIsActive = false;
		FString DisplayValue;
		void Reset() { bIsActive = false; DisplayValue.Reset(); }
	};
	FDebugState DebugState;
};
