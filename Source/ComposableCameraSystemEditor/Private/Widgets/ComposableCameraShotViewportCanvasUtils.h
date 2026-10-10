// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace ComposableCameraSystem::ShotEditorCanvas
{
// FCanvas scales its drawing transform; FSceneViewport mouse coordinates remain physical pixels.
inline float ValidScale(float DPIScale)
{
	return FMath::IsFinite(DPIScale) && DPIScale > 0.f ? DPIScale : 1.f;
}

inline FVector2D ViewportToCanvas(const FVector2D& PixelPosition, float DPIScale)
{
	return PixelPosition / ValidScale(DPIScale);
}

inline FVector2D CanvasToViewport(const FVector2D& CanvasPosition, float DPIScale)
{
	return CanvasPosition * ValidScale(DPIScale);
}

inline FBox2D CanvasHitAreaToViewport(const FBox2D& CanvasArea, float DPIScale)
{
	return FBox2D(CanvasToViewport(CanvasArea.Min, DPIScale), CanvasToViewport(CanvasArea.Max, DPIScale));
}
}
