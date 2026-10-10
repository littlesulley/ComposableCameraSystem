// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/ComposableCameraShotViewportCanvasUtils.h"

namespace ComposableCameraSystem::ShotViewportOverlay
{
/** Relative angular editing in the authored BasisFrame, independent of scene scale. */
inline FVector2D DragOrbitDirection(FVector2D Direction, FVector2D PixelDelta, float DpiScale, float Speed = 1.f)
{
	if (PixelDelta.IsNearlyZero()) return Direction;
	const FVector2D Delta = PixelDelta / ShotEditorCanvas::ValidScale(DpiScale) * (.45f * Speed);
	return FVector2D(FRotator::NormalizeAxis(Direction.X + Delta.X), FMath::Clamp(Direction.Y - Delta.Y, -89.5, 89.5));
}

struct FOrbitControlLayout
{
	FBox2D Panel{ ForceInit };
	FVector2D Center = FVector2D::ZeroVector;
	float Radius = 0.f;
	bool IsVisible() const { return Radius > 0.f; }
	bool Hit(FVector2D CanvasPoint) const { return IsVisible() && (CanvasPoint - Center).SizeSquared() <= FMath::Square(Radius); }
};

/** A logical Canvas-space control, anchored inside the actual camera image. */
inline FOrbitControlLayout OrbitLayout(const FBox2D& Image)
{
	FOrbitControlLayout Result;
	if (!Image.bIsValid || Image.GetSize().X < 180.f || Image.GetSize().Y < 196.f) return Result;
	const FVector2D Origin(Image.Min.X + 8.f, Image.Max.Y - 152.f);
	Result.Panel = FBox2D(Origin, Origin + FVector2D(164.f, 144.f));
	Result.Center = Origin + FVector2D(82.f, 70.f);
	Result.Radius = 40.f;
	return Result;
}

/** Fixed oblique view of the local basis. X is depth, Y horizontal, Z vertical. */
inline FVector OrbitGlobePoint(FVector2D Direction)
{
	static const FQuat View = FRotator(20.f, -135.f, 0.f).Quaternion().Inverse();
	return View.RotateVector(FRotator(Direction.Y, Direction.X, 0.f).Vector());
}

struct FHudLayout
{
	FBox2D Camera{ ForceInit };
	FBox2D Composition{ ForceInit };
	float Scale = 0.f;
	static constexpr float HeaderHeight = 24.f;
	static constexpr float RowHeight = 18.f;
	int32 Rows(const FBox2D& Card) const
	{
		// Tolerate rectangle subtraction roundoff at scaled row boundaries.
		return Card.bIsValid && Scale > 0.f ? FMath::Max(0, FMath::FloorToInt((Card.GetSize().Y / Scale - HeaderHeight) / RowHeight + 1.e-4)) : 0;
	}
};

/** One compact upper-left group, uniformly scaled with the camera image, independent of guides. */
inline FHudLayout HudLayout(const FBox2D& Image)
{
	FHudLayout Result;
	if (!Image.bIsValid) return Result;
	const FVector2D Size = Image.GetSize();
	if (Size.X <= 16. || Size.Y <= 16.) return Result;
	const FVector2D Origin = Image.Min + FVector2D(8., 8.);
	const double ProportionalScale = .85 * FMath::Min(Size.X / 1280., Size.Y / 720.);
	// Reserve the possible orbit footprint even when Guides is off: toggling guides must not resize the HUD.
	const FOrbitControlLayout Orbit = OrbitLayout(Image);
	const double AvailableHeight = Orbit.IsVisible() ? Orbit.Panel.Min.Y - Origin.Y - 8. : Size.Y - 16.;
	Result.Scale = float(FMath::Min(ProportionalScale, FMath::Min((Size.X - 16.) / 348., AvailableHeight / 362.)));
	Result.Camera = FBox2D(Origin, Origin + FVector2D(348., 168.) * Result.Scale);
	const FVector2D CompositionOrigin = Origin + FVector2D(0., 176.) * Result.Scale;
	Result.Composition = FBox2D(CompositionOrigin, CompositionOrigin + FVector2D(348., 186.) * Result.Scale);
	return Result;
}
}
