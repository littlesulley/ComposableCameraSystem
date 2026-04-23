// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

/**
 * Editor style.
 */
struct FComposableCameraEditorStyle final : public FSlateStyleSet
{
public:
	FComposableCameraEditorStyle();
	virtual ~FComposableCameraEditorStyle();

	static TSharedRef<FComposableCameraEditorStyle> Get();

private:
	static TSharedPtr<FComposableCameraEditorStyle> Singleton;
};

/**
 * Centralised color palette for the Composable Camera editor.
 *
 * Previously every pin-type color lived inline in the schema's
 * `GetPinTypeColor` switch and every node-title color lived inline on each
 * `UEdGraphNode::GetNodeTitleColor` override. Pulling them here makes theme
 * tweaks one-file-edit, keeps pin colors consistent across any future
 * connection-drawing policies that need the same palette, and lets the design
 * doc point at a concrete symbol instead of a literal per section.
 *
 * `FLinearColor` has a non-trivial constructor so these have to be `const`
 * rather than `constexpr`; definitions live in `ComposableCameraEditorStyle.cpp`.
 */
struct FComposableCameraEditorColors
{
	// ─── Pin type colors (match schema `GetPinTypeColor`) ─────────────────

	/** Execution pins — same visual as Blueprint exec. */
	static const FLinearColor PinExec;

	/** Boolean pins — Blueprint red. */
	static const FLinearColor PinBool;

	/** Integer pins — teal. */
	static const FLinearColor PinInt;

	/** Float / double pins — green. */
	static const FLinearColor PinFloat;

	/** Vector-family pins (FVector / FVector2D / FVector4) — gold. */
	static const FLinearColor PinVector;

	/** Rotator pins — blue-purple. */
	static const FLinearColor PinRotator;

	/** Transform pins — orange. */
	static const FLinearColor PinTransform;

	/** Generic struct pins (anything that is not one of the math types above)
	 *  — saturated blue, distinct from the object pin's slightly darker blue. */
	static const FLinearColor PinStructGeneric;

	/** UObject / Actor pins — blue, slightly darker than `PinStructGeneric`. */
	static const FLinearColor PinObject;

	/** FName pins — pink (#FFC0CB), per EditorDesignDoc spec. */
	static const FLinearColor PinName;

	/** Byte / enum pins — bright cyan (#00BFFF), per EditorDesignDoc spec. */
	static const FLinearColor PinByteEnum;

	/** Delegate pins — saturated red (distinct from bool's darker 0.9 red). */
	static const FLinearColor PinDelegate;

	/** Fallback for any PinCategory the schema did not otherwise handle. */
	static const FLinearColor PinDefault;

	// ─── Node title colors (match per-node `GetNodeTitleColor` overrides) ──

	/** Regular camera nodes — teal, matching the Camera Type Asset color. */
	static const FLinearColor CameraNodeTitle;

	/** Compute nodes — warm amber, visually grouped with the BeginPlay Start
	 *  sentinel but slightly lighter. */
	static const FLinearColor ComputeNodeTitle;

	/** Variable Get / Set graph nodes — desaturated purple. */
	static const FLinearColor VariableNodeTitle;

	/** Start sentinel — green. */
	static const FLinearColor StartNodeTitle;

	/** Output sentinel — red. */
	static const FLinearColor OutputNodeTitle;

	/** BeginPlay Start sentinel — amber, same family as `ComputeNodeTitle`
	 *  but a shade darker so the sentinel reads as the "root" of the compute
	 *  chain. */
	static const FLinearColor BeginPlayStartNodeTitle;
};

/**
 * Named Slate padding / spacing constants used by the editor's authored
 * widgets. Extracted so changing the overall density of editor dialogs is a
 * one-file edit and so reviewers don't have to reverse-engineer what, e.g.,
 * `.Padding(16.f, 16.f, 16.f, 12.f)` was trying to say.
 *
 * These are tuned for modal dialogs owned by the Composable Camera editor
 * (type conflict prompts, variable Create dialogs). Graph-node Slate widgets
 * have their own layout constraints and continue to use local constants
 * sized to their specific layouts.
 */
struct FComposableCameraEditorPaddings
{
	/** Standard horizontal inset for dialog content. */
	static constexpr float DialogLR = 16.f;

	/** Standard vertical inset for dialog top / bottom edges. */
	static constexpr float DialogTB = 16.f;

	/** Tighter bottom inset for the first content block of a dialog (sits
	 *  above a second block, so the gap between them reads as a single
	 *  "between-rows" distance rather than a full dialog margin). */
	static constexpr float DialogBetweenRowsTB = 12.f;

	/** Horizontal inset between adjacent inline widgets (buttons in a row,
	 *  labels beside a value editor, etc.). */
	static constexpr float InnerGap = 4.f;
};
