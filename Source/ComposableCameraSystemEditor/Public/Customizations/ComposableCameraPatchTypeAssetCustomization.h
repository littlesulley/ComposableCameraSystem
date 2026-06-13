// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FPropertyEditorModule;
class IDetailLayoutBuilder;

/**
 * Details-panel customization for UComposableCameraPatchTypeAsset.
 *
 * Hides three properties inherited from UComposableCameraTypeAsset that have no
 * meaning in a Patch context:
 *
 * - EnterTransition / ExitTransition - these are camera-vs-camera pose
 * blenders triggered when a Camera enters/leaves the EvaluationTree. A
 * Patch never enters the tree (it is a Director-level post-process
 * overlay), so these fields would never be read by any code path. Worse,
 * leaving them visible would suggest they overlap with the Patch's own
 * envelope (DefaultEnterDuration / DefaultExitDuration / DefaultEaseType),
 * which is the actual "fade in/out" mechanism.
 *
 * - bDefaultPreserveCameraPose - controls "preserve last camera's pose when
 * resuming this camera from the context stack". Patches are never resumed;
 * they are added/removed in place. The flag has no effect on Patch
 * evaluators.
 *
 * Hiding these in the Details panel is the cleanest fix: the inherited
 * properties remain on the base UCLASS for the regular TypeAsset case, but
 * Patch authors never see them. The Patch's own envelope / lifetime / layer
 * fields (declared on UComposableCameraPatchTypeAsset) remain fully visible.
 *
 * The graph editor toolkit is not affected - the toolkit's transition picker
 * (if any) is keyed off the asset class identity at toolkit construction time.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraPatchTypeAssetCustomization: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
