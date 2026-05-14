// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FPropertyEditorModule;

/**
 * IDetailCustomization for UMovieSceneComposableCameraPatchSection.
 *
 * Hides the per-call expiration override fields (`Params.bOverrideExpirationType`,
 * `Params.ExpirationType`, `Params.bOverrideDuration`, `Params.Duration`) on
 * the Section's Details panel only. The fields stay live on the underlying
 * `FComposableCameraPatchActivateParams` struct - the BP `AddCameraPatch`
 * authoring path still surfaces them - but on a Sequencer Section the
 * section's range *is* the patch lifetime: TrackInstance fires `OnInputRemoved`
 * when the playhead exits the section, which tears down the LS Component
 * overlay. Letting designers set ExpirationType / Duration there would be a
 * dead UPROPERTY; the overlay path never reads either field.
 *
 * LayerIndex pair stays visible - it IS read by
 * `UComposableCameraLevelSequenceComponent::ApplySequencerPatchOverlays`'s
 * sort step, so it remains a meaningful authoring surface on the Section.
 */
class FComposableCameraPatchSectionDetails: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
};
