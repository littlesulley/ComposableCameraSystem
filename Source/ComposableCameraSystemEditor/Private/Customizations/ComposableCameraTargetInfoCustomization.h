// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FPropertyEditorModule;
class IPropertyHandle;
class SSearchableComboBox;
class USkeletalMeshComponent;

/**
 * IPropertyTypeCustomization for `FComposableCameraTargetInfo`.
 *
 * Replaces the default free-text rendering of the `BoneName` UPROPERTY with
 * a searchable combo box populated from the resolved `Actor`'s first
 * `USkeletalMeshComponent` — listing every bone in the reference skeleton
 * plus every socket. Designers no longer have to remember exact bone names
 * (which are case-sensitive `FName` keys) and don't need a SkelMesh editor
 * window open alongside the Shot Editor / Camera Type Asset to look them up.
 *
 * Resolution:
 *   - Walk to the sibling `Actor: TSoftObjectPtr<AActor>` handle.
 *   - `Actor.Get()` (no force-load on hot path); fall back to
 *     `LoadSynchronous` once if `Get` returns null in editor world (the
 *     soft-ref typically resolves immediately for in-memory level actors).
 *   - First `USkeletalMeshComponent` on the actor — the same priority order
 *     `FComposableCameraTargetInfo::ResolveWorldPoint` uses at runtime.
 *   - Bone names from `RefSkeleton.GetRefBoneInfo()` + socket names from
 *     `SkelComp->GetAllSocketNames()`, merged + deduped + sorted.
 *
 * Edge cases:
 *   - Actor unset / not loaded / no SkelMesh → combo shows a "(no skeletal
 *     mesh found)" placeholder and is disabled. Designer assigns Actor
 *     first, then re-opens the combo.
 *   - `bUseBoneAsPivot == false` → combo is disabled (matches the existing
 *     `EditCondition` on the `BoneName` UPROPERTY). Designer ticks
 *     "Use Bone As Pivot" first.
 *   - Multi-edit (multiple struct instances selected): the combo reads the
 *     first selection's actor and lists those bones; selection writes the
 *     same bone name to all selected instances. Inconsistent values across
 *     selection display as the standard "<multiple values>" placeholder.
 *
 * No conflict with the Shot Editor viewport's anchor handles — those
 * drive Placement / Aim screen positions (LMB drag in Drag mode) and
 * trigger in-viewport bone picking via the RMB context menu. Bone picking
 * via the Details combo is a separate authoring surface — both write to
 * the same `BoneName` UPROPERTY. The combo is the only path that can
 * clear (set to None).
 *
 * Lives in the editor module and registers against the runtime struct's
 * StaticStruct — same pattern as the other Customizations.
 */
class FComposableCameraTargetInfoCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		class IDetailChildrenBuilder& StructBuilder,
		IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** Refresh `BoneOptions` from the currently-resolved Actor's SkelMesh.
	 *  Called once at construction and on every `Actor` UPROPERTY change. */
	void RefreshBoneOptions();

	/** Resolve the first `USkeletalMeshComponent` on the effective target
	 *  actor. Effective actor = LS Section override binding's resolved
	 *  actor when present (Phase E `FComposableCameraShotTargetActorOverride`
	 *  routed through the open Sequencer), else the directly-authored
	 *  `Actor` UPROPERTY. Returns null when neither path resolves. */
	USkeletalMeshComponent* ResolveSkelMeshComponent() const;

	/** Resolve the LS Section override actor for this Target's index, or
	 *  null if no override is active / no host Section / no open Sequencer.
	 *  Walks `IPropertyHandle::GetOuterObjects` for the Sequencer Section
	 *  Details path; falls back to `FComposableCameraShotEditor::GetCurrentLiveHost`
	 *  for the Shot Editor `IStructureDetailsView` path (where the
	 *  `FStructOnScope` carries no host UObject). Convenience wrapper around
	 *  `ResolveLSOverrideContext` — returns just the actor pointer. */
	class AActor* ResolveLSOverrideActor() const;

	/** Resolve the full LS Section override context: bound Actor + the
	 *  Sequencer-scope context the binding came from (LS sequence name +
	 *  binding display name). Used by the "Effective Actor" Details row to
	 *  display "<Actor>  (LS: <Sequence> / <Binding>)" so designers can see
	 *  WHICH LS the override is sourced from when multiple are open. Out
	 *  fields are left empty when no override resolves; the Actor return
	 *  is the same as `ResolveLSOverrideActor`. */
	class AActor* ResolveLSOverrideContext(
		FText& OutSequenceDisplayName,
		FText& OutBindingDisplayName) const;

	/** Compute this `FComposableCameraTargetInfo`'s position in its outer
	 *  `Shot.Targets` array — needed to look up the matching
	 *  `FComposableCameraShotTargetActorOverride::TargetIndex`. Returns
	 *  `INDEX_NONE` when the property handle chain doesn't resolve to an
	 *  array element. */
	int32 ResolveTargetIndex() const;

	/** Build the combo widget. Captures weak handles only — no `this`
	 *  capture — so combo callbacks survive customization tear-down without
	 *  dereferencing a dead pointer (Slate destroys widgets asynchronously). */
	TSharedRef<SWidget> BuildBoneCombo();

	/** Build the read-only "Effective Target Actor" label that surfaces the
	 *  LS Section override binding's resolved actor when active. Always
	 *  present in the UI (static row); the label text adapts: actor name
	 *  when override is active and resolves, "(no override)" otherwise. */
	TSharedRef<SWidget> BuildEffectiveActorLabel();

private:
	/** Owning struct handle. Held strongly so callbacks keep it alive. */
	TSharedPtr<IPropertyHandle> StructHandle;

	/** Sibling handle to `FComposableCameraTargetInfo::Actor`. Used for
	 *  SkelMesh resolution + bound to a value-changed delegate that
	 *  refreshes the bone list when the actor is reassigned. Held
	 *  *strongly* — `IPropertyHandle::GetChildHandle` returns a fresh
	 *  `TSharedPtr` each call and the property tree does not always retain
	 *  a parallel reference once the local SharedPtr goes out of scope, so
	 *  a `TWeakPtr` would Pin to null after CustomizeChildren returned. */
	TSharedPtr<IPropertyHandle> ActorHandle;

	/** Sibling handle to `FComposableCameraTargetInfo::BoneName`. Read for
	 *  the combo's current label; written on selection change. Held
	 *  strongly for the same reason as `ActorHandle`. */
	TSharedPtr<IPropertyHandle> BoneNameHandle;

	/** Combo data source. `TSharedPtr<FString>` is what `SSearchableComboBox`
	 *  expects (matches the engine widget's contract). First entry is always
	 *  the literal `"(none)"` so designers can clear the bone selection
	 *  without flipping `bUseBoneAsPivot`.
	 *
	 *  Heap-stored via TSharedRef so the combo widget — which captures the
	 *  raw `TArray*` through `SLATE_ARGUMENT(OptionsSource)` — keeps the
	 *  array alive across customization tear-down. Slate destroys widgets
	 *  asynchronously: a panel rebuild can release the customization while
	 *  a deferred paint / tick still reads `OptionsSource`. The combo also
	 *  captures this SharedRef in its `OnSelectionChanged` / `RefreshOptions`
	 *  lambdas to extend the array's lifetime past the customization's. */
	TSharedRef<TArray<TSharedPtr<FString>>> BoneOptions =
		MakeShared<TArray<TSharedPtr<FString>>>();

	/** Combo widget itself — kept around so we can call `RefreshOptions()`
	 *  from the Actor-changed delegate. */
	TSharedPtr<SSearchableComboBox> BoneCombo;
};
