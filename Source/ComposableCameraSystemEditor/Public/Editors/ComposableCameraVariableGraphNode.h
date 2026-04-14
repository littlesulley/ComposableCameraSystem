// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "ComposableCameraVariableGraphNode.generated.h"

struct FComposableCameraInternalVariable;
class UComposableCameraTypeAsset;

/**
 * EdGraphNode that represents a single internal-variable read (Get) or write (Set)
 * inside the visual camera type graph.
 *
 * Get nodes expose a single output pin whose type matches the variable's declared type.
 * They are pure data conduits — reads happen implicitly whenever the camera node
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
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraVariableGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/**
	 * Stable identity of the internal variable this node reads from / writes to.
	 *
	 * When the user renames a variable in the Details panel, VariableName on the
	 * owning type asset changes but VariableGuid stays the same — so this node
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

	// ─── Well-Known Pin Names ──────────────────────────────────────────

	/** Output pin on Get nodes / input pin on Set nodes. */
	static const FName PN_Value;

	/** Exec input pin on Set nodes (absent on Get nodes). */
	static const FName PN_ExecIn;

	/** Exec output pin on Set nodes (absent on Get nodes). */
	static const FName PN_ExecOut;

	// ─── UEdGraphNode Interface ────────────────────────────────────────

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PostPasteNode() override;

	// ─── Helpers ───────────────────────────────────────────────────────

	/** Find the variable declaration on the owning type asset. Returns nullptr if missing. */
	const FComposableCameraInternalVariable* FindVariable() const;

	/** Find the owning camera type asset by walking up through the graph's outer. */
	UComposableCameraTypeAsset* GetOwningTypeAsset() const;

	/** Rebuild pins from the current variable lookup (call after renames / type changes). */
	void ReconstructPins();

	// ─── Runtime Debug State ──────────────────────────────────────────────

	/** Transient debug state pushed by the toolkit's debug ticker during PIE. */
	struct FDebugState
	{
		bool bIsActive = false;
		FString DisplayValue;
		void Reset() { bIsActive = false; DisplayValue.Reset(); }
	};
	FDebugState DebugState;
};
