// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraDebugDrawSink.h"

#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

FComposableCameraLiveDebugDrawSink::FComposableCameraLiveDebugDrawSink(UWorld* InWorld)
	: World(InWorld)
{
}

void FComposableCameraLiveDebugDrawSink::DrawLine(
	const FVector& Start,
	const FVector& End,
	const FColor& Color,
	float Thickness,
	uint8 DepthPriority)
{
	if (!World)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	DrawDebugLine(World, Start, End, Color, false, -1.0f, DepthPriority, Thickness);
#endif
}

void FComposableCameraLiveDebugDrawSink::DrawPoint(
	const FVector& Location,
	const FColor& Color,
	float Size,
	uint8 DepthPriority)
{
	if (!World)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	DrawDebugPoint(World, Location, Size, Color, false, -1.0f, DepthPriority);
#endif
}

void FComposableCameraLiveDebugDrawSink::DrawSphere(
	const FVector& Center,
	float Radius,
	const FColor& Color,
	uint8 Alpha,
	uint8 DepthPriority,
	bool bSolid)
{
	if (!World)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (bSolid)
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(World, Center, Radius, Color, Alpha, 12, DepthPriority);
		return;
	}

	DrawDebugSphere(
		World,
		Center,
		Radius,
		12,
		FColor(Color.R, Color.G, Color.B, Alpha),
		false,
		-1.0f,
		DepthPriority);
#endif
}

void FComposableCameraLiveDebugDrawSink::DrawBox(
	const FVector& Center,
	const FVector& Extent,
	const FQuat& Rotation,
	const FColor& Color,
	uint8 DepthPriority)
{
	if (!World)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	DrawDebugBox(World, Center, Extent, Rotation, Color, false, -1.0f, DepthPriority);
#endif
}

void FComposableCameraLiveDebugDrawSink::DrawCameraFrustum(
	const FComposableCameraTracePose& Pose,
	const FColor& Color,
	uint8 DepthPriority)
{
	if (!World)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	DrawDebugCamera(World, Pose.Location, Pose.Rotation, Pose.FieldOfView, 100.0f, Color, false, -1.0f, DepthPriority);
#endif
}

FComposableCameraPrimitiveCaptureSink::FComposableCameraPrimitiveCaptureSink(
	TArray<FComposableCameraDebugPrimitive>& InPrimitives)
	: Primitives(InPrimitives)
{
}

void FComposableCameraPrimitiveCaptureSink::DrawLine(
	const FVector& Start,
	const FVector& End,
	const FColor& Color,
	float Thickness,
	uint8 DepthPriority)
{
	Primitives.Add(FComposableCameraDebugPrimitive::MakeLine(Start, End, Color, Thickness, DepthPriority));
}

void FComposableCameraPrimitiveCaptureSink::DrawPoint(
	const FVector& Location,
	const FColor& Color,
	float Size,
	uint8 DepthPriority)
{
	Primitives.Add(FComposableCameraDebugPrimitive::MakePoint(Location, Color, Size, DepthPriority));
}

void FComposableCameraPrimitiveCaptureSink::DrawSphere(
	const FVector& Center,
	float Radius,
	const FColor& Color,
	uint8 Alpha,
	uint8 DepthPriority,
	bool bSolid)
{
	Primitives.Add(FComposableCameraDebugPrimitive::MakeSphere(Center, Radius, Color, Alpha, DepthPriority, bSolid));
}

void FComposableCameraPrimitiveCaptureSink::DrawBox(
	const FVector& Center,
	const FVector& Extent,
	const FQuat& Rotation,
	const FColor& Color,
	uint8 DepthPriority)
{
	Primitives.Add(FComposableCameraDebugPrimitive::MakeBox(Center, Extent, Rotation, Color, DepthPriority));
}

void FComposableCameraPrimitiveCaptureSink::DrawCameraFrustum(
	const FComposableCameraTracePose& Pose,
	const FColor& Color,
	uint8 DepthPriority)
{
	Primitives.Add(FComposableCameraDebugPrimitive::MakeCameraFrustum(Pose, Color, DepthPriority));
}
