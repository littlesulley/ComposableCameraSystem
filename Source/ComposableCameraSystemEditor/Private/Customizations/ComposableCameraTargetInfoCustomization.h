// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FPropertyEditorModule;
class IPropertyHandle;
class SSearchableComboBox;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * IPropertyTypeCustomization for `FComposableCameraTargetInfo`.
 *
 * Replaces the default free-text rendering of the `BoneName` UPROPERTY
 * with a searchable combo box populated from the resolved target mesh:
 * LS-bound Actor, authored Actor, then editor-only `EditorPreviewMesh`.
 * Designers no longer have to remember exact bone names.
 *
 * Resolution:
 *   - Resolve the LS Section TargetActorOverrides actor when present.
 *   - Else resolve the sibling `Actor: TSoftObjectPtr<AActor>` handle.
 *   - Else resolve editor-only `EditorPreviewMesh`.
 *   - Bone names from the mesh reference skeleton + socket names from
 *     `USkeletalMesh::GetActiveSocketList()`, merged + deduped + sorted.
 *
 * Edge cases:
 *   - No actor / preview mesh, or no SkelMesh -> combo shows a
 *     "(no skeletal mesh found)" placeholder and is disabled.
 *   - `bUseBoneAsPivot == false` -> combo is disabled (matches the
 *     existing `EditCondition` on the `BoneName` UPROPERTY).
 *   - Multi-edit reads the first selection source and writes the chosen
 *     bone name to all selected instances.
 *
 * The combo writes back through the normal `IPropertyHandle` path, so
 * transactions, multi-edit, and undo/redo stay in the PropertyEditor
 * pipeline.
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

	/** Resolve the SkeletalMesh asset for the effective target
	 *  actor. Effective actor = LS Section override binding's resolved
	 *  actor when present (Phase E `FComposableCameraShotTargetActorOverride`
	 *  routed through the open Sequencer), else the directly-authored
	 *  `Actor` UPROPERTY, else the editor-only preview mesh asset. Returns
	 *  null when none of those paths resolves. */
	USkeletalMesh* ResolveSkeletalMesh() const;

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

#if WITH_EDITORONLY_DATA
	/** Sibling handle to `FComposableCameraTargetInfo::EditorPreviewMesh`.
	 *  Used only by editor authoring surfaces as a fallback source for the
	 *  bone picker and viewport preview when no runtime actor/LS override
	 *  resolves. */
	TSharedPtr<IPropertyHandle> EditorPreviewMeshHandle;
#endif

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
