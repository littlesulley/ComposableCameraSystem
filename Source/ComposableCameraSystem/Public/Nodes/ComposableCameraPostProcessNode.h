// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Engine/Scene.h"
#include "ComposableCameraPostProcessNode.generated.h"

/**
 * Node that applies post-process settings to the camera pose.
 *
 * Works like a PostProcessVolume but scoped to a single camera type.
 * Only properties whose bOverride_* flag is true take effect; all others
 * pass through from the camera component's baseline or from earlier nodes.
 *
 * The settings are applied once per tick via
 * FPostProcessUtils::OverridePostProcessSettings onto OutCameraPose.PostProcessSettings.
 * Multiple PostProcess nodes in the same camera stack compose in execution order
 * (later nodes override earlier ones for the same bOverride_* property).
 *
 * No pins are declared. FPostProcessSettings is configured entirely through
 * the Details panel, matching the PostProcessVolume workflow UE artists are
 * already familiar with.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Applies post-process settings to the camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraPostProcessNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPostProcessNode() { PaletteCategory = TEXT("Post Process"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;

public:
	/**
	 * Post-process settings to apply. Toggle individual bOverride_* flags
	 * to control which properties this node contributes. Properties with
	 * their override flag off are left untouched.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PostProcess")
	FPostProcessSettings PostProcessSettings;
};
