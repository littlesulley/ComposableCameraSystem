// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "DataAssets/ComposableCameraShot.h"
#include "ShowFlags.h"
#include "CineCameraComponent.h"
#include "UObject/UObjectGlobals.h"

namespace ComposableCameraSystem::ShotViewportDisplay
{
inline bool ShowFollowHandle(const FComposableCameraShot& Shot)
{
	return Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen;
}
inline bool ShowLookAtHandle(const FComposableCameraShot& Shot, bool bLookAtSelected)
{
	return bLookAtSelected && Shot.Aim.Mode == EShotAimMode::LookAtAnchor;
}
inline bool ShowOrbitControl(const FComposableCameraShot& Shot, bool bFollowSelected)
{
	return bFollowSelected && Shot.Placement.Mode == EShotPlacementMode::AnchorOrbit;
}
/** Match CineCamera's filmback, squeeze and crop without changing the component. */
inline float CameraAspectRatio(const UCineCameraComponent* Camera)
{
	if (!Camera) Camera = GetDefault<UCineCameraComponent>();
	const float Ratio = Camera->CropSettings.AspectRatio > 0.f ? Camera->CropSettings.AspectRatio
		: (Camera->Filmback.SensorHeight > 0.f
			? Camera->Filmback.SensorWidth * Camera->LensSettings.SqueezeFactor / Camera->Filmback.SensorHeight : 0.f);
	return FMath::IsFinite(Ratio) && Ratio > UE_SMALL_NUMBER ? Ratio : 16.f / 9.f;
}
/** Local ShotEditor viewport only. Canvas composition guides remain independent. */
inline void ConfigurePreviewFlags(FEngineShowFlags& Flags, bool bLevelWorld)
{
	Flags.SetGame(true);
	Flags.SetEditor(false);
	Flags.SetModeWidgets(false);
	Flags.SetCompositeEditorPrimitives(false);
	Flags.SetSelection(false);
	Flags.SetSelectionOutline(false);
	Flags.SetBounds(false);
	Flags.SetCollision(false);
	Flags.SetCollisionPawn(false);
	Flags.SetCollisionVisibility(false);
	Flags.SetGrid(!bLevelWorld);
}
}
