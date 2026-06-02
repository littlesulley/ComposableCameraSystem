// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FPropertyEditorModule;

/**
 * IDetailCustomization for UMovieSceneComposableCameraShotSection.
 *
 * Hides two UPROPERTYs from the Section's Details panel:
 * - `TargetActorOverrides` - fully driven by the right-click context menu
 * "Bind Target Actors " submenu, which adds Sequencer-binding picker UX
 * (radio-button per binding, transactional, current selection visible).
 * The bare TArray editor underneath is the same data, just less ergonomic.
 * Hiding it removes the redundant entry point so designers have one canonical
 * authoring surface.
 * - `EnterTransition` - fully driven by the right-click context menu
 * "Set Enter Transition " submenu. Same rationale: the menu
 * surfaces the soft-pointer asset picker plus a Clear entry, with the
 * current selection labeled in the parent menu - friendlier than the bare
 * `TSoftObjectPtr` Details slot.
 *
 * Both fields stay live as `EditAnywhere` UPROPERTYs at the reflection level
 * so serialization, diff/merge, and Blueprint mutator paths still work; only
 * the auto-generated Details row is hidden.
 */
class FComposableCameraShotSectionDetails: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};
