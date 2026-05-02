// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class IPropertyHandle;
class IPropertyHandleArray;
class SWidget;
class FMenuBuilder;

/**
 * Shared helper for any `int32 TargetIndex`-style UPROPERTY that selects an
 * entry in the owning `Shot.Targets` array. Used by:
 *   - `FShotPlacement::BasisActorIndex`
 *   - `FComposableCameraAnchorSpec::TargetIndex`
 *   - `FComposableCameraAnchorTargetWeight::TargetIndex`
 *
 * All three live at different depths inside `FComposableCameraShot` but the
 * authoring intent is identical: pick one of the Shot's authored Targets
 * entries by index. The bare integer spinner silently breaks under reorder
 * (the index stays but the actor it points to changes); the dropdown reads
 * the live array each open and keeps labels in sync.
 *
 * Walking strategy: from the caller's `IPropertyHandle`, walk
 * `GetParentHandle()` upwards until a child named `"Targets"` resolves to a
 * valid array handle. Stops at the first hit so the helper works for any
 * embedding depth — single-level (Shot → Placement) and double-level
 * (Shot → Aim → AimAnchor → TargetIndex) and triple-level (Shot → Placement
 * → PlacementAnchor → WeightedTargets[i] → TargetIndex).
 *
 * No state, no allocations on the hot path beyond the SComboButton itself.
 */
struct FShotTargetIndexCombo
{
	/** Walk parent handles upward from `Start` looking for a sibling/ancestor
	 *  named `"Targets"`. Returns the array handle, or null when no such
	 *  ancestor exists (detached / unit-test wrap / multi-edit conflict). */
	static TSharedPtr<IPropertyHandleArray> ResolveTargetsArrayUpwards(
		const TSharedPtr<IPropertyHandle>& Start);

	/** Format `"<i> — <ActorLabel>"` for entry `Idx`. Loaded actor's
	 *  `GetActorLabel()` first, soft-path's tail name when unloaded,
	 *  `"(unset)"` for empty slots. Out-of-range surfaces as
	 *  `"<idx> — (invalid)"`. */
	static FText FormatTargetEntryLabel(
		const TSharedPtr<IPropertyHandleArray>& TargetsArray, int32 Idx);

	/** Combo button label — formats the current `IndexHandle` value through
	 *  `FormatTargetEntryLabel`. Multi-edit with mixed values returns
	 *  `"<multiple values>"`. */
	static FText GetCurrentSelectionText(
		const TSharedPtr<IPropertyHandle>& IndexHandle,
		const TSharedPtr<IPropertyHandleArray>& TargetsArray);

	/** Fill `MenuBuilder` with one entry per Targets index, each entry
	 *  writing its index into `IndexHandle` on click. Empty / unreachable
	 *  Targets surface as a single disabled hint entry. */
	static void BuildIndexMenu(
		FMenuBuilder& MenuBuilder,
		const TWeakPtr<IPropertyHandle>& WeakIndexHandle,
		const TSharedPtr<IPropertyHandleArray>& TargetsArray);

	/** Build the full SComboButton + menu widget for `IndexHandle`.
	 *  `IndexHandle` is captured STRONGLY (TSharedPtr by value into the
	 *  combo's lambdas) — `IPropertyHandle::GetChildHandle` returns a
	 *  fresh `TSharedPtr` each call and the property tree does not
	 *  retain a parallel reference, so a weak capture pins to null the
	 *  moment the customization's local handle goes out of scope and the
	 *  combo would render "(no handle)" forever. Menu rebuilds resolve
	 *  the Targets array each open via `ResolveTargetsArrayUpwards(IndexHandle)`. */
	static TSharedRef<SWidget> Build(const TSharedRef<IPropertyHandle>& IndexHandle);
};
