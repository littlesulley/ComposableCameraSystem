// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"

class UWorld;

class COMPOSABLECAMERASYSTEM_API FComposableCameraDebugDrawSink
{
public:
	virtual ~FComposableCameraDebugDrawSink() = default;

	virtual bool ShouldForceDrawAllNodeGizmos() const { return false; }
	virtual bool ShouldForceDrawAllTransitionGizmos() const { return false; }

	virtual void DrawLine(
		const FVector& Start,
		const FVector& End,
		const FColor& Color,
		float Thickness,
		uint8 DepthPriority) = 0;

	virtual void DrawPoint(
		const FVector& Location,
		const FColor& Color,
		float Size,
		uint8 DepthPriority) = 0;

	virtual void DrawSphere(
		const FVector& Center,
		float Radius,
		const FColor& Color,
		uint8 Alpha,
		uint8 DepthPriority,
		bool bSolid,
		int32 Segments = 12,
		float Thickness = 0.0f,
		const TCHAR* Label = nullptr) = 0;

	virtual void DrawBox(
		const FVector& Center,
		const FVector& Extent,
		const FQuat& Rotation,
		const FColor& Color,
		uint8 DepthPriority,
		float Thickness = 0.0f) = 0;

	virtual void DrawPlane(
		const FVector& Center,
		const FVector& Normal,
		const FVector2D& Extents,
		const FColor& Color,
		uint8 DepthPriority) = 0;

	virtual void DrawCameraFrustum(
		const FComposableCameraTracePose& Pose,
		const FColor& Color,
		uint8 DepthPriority,
		float Scale = 1.0f) = 0;
};

class COMPOSABLECAMERASYSTEM_API FComposableCameraLiveDebugDrawSink final : public FComposableCameraDebugDrawSink
{
public:
	explicit FComposableCameraLiveDebugDrawSink(UWorld* InWorld);

	virtual void DrawLine(
		const FVector& Start,
		const FVector& End,
		const FColor& Color,
		float Thickness,
		uint8 DepthPriority) override;

	virtual void DrawPoint(
		const FVector& Location,
		const FColor& Color,
		float Size,
		uint8 DepthPriority) override;

	virtual void DrawSphere(
		const FVector& Center,
		float Radius,
		const FColor& Color,
		uint8 Alpha,
		uint8 DepthPriority,
		bool bSolid,
		int32 Segments = 12,
		float Thickness = 0.0f,
		const TCHAR* Label = nullptr) override;

	virtual void DrawBox(
		const FVector& Center,
		const FVector& Extent,
		const FQuat& Rotation,
		const FColor& Color,
		uint8 DepthPriority,
		float Thickness = 0.0f) override;

	virtual void DrawPlane(
		const FVector& Center,
		const FVector& Normal,
		const FVector2D& Extents,
		const FColor& Color,
		uint8 DepthPriority) override;

	virtual void DrawCameraFrustum(
		const FComposableCameraTracePose& Pose,
		const FColor& Color,
		uint8 DepthPriority,
		float Scale) override;

private:
	UWorld* World = nullptr;
};

class COMPOSABLECAMERASYSTEM_API FComposableCameraPrimitiveCaptureSink final : public FComposableCameraDebugDrawSink
{
public:
	explicit FComposableCameraPrimitiveCaptureSink(TArray<FComposableCameraDebugPrimitive>& InPrimitives);

	virtual bool ShouldForceDrawAllNodeGizmos() const override { return true; }
	virtual bool ShouldForceDrawAllTransitionGizmos() const override { return true; }

	virtual void DrawLine(
		const FVector& Start,
		const FVector& End,
		const FColor& Color,
		float Thickness,
		uint8 DepthPriority) override;

	virtual void DrawPoint(
		const FVector& Location,
		const FColor& Color,
		float Size,
		uint8 DepthPriority) override;

	virtual void DrawSphere(
		const FVector& Center,
		float Radius,
		const FColor& Color,
		uint8 Alpha,
		uint8 DepthPriority,
		bool bSolid,
		int32 Segments = 12,
		float Thickness = 0.0f,
		const TCHAR* Label = nullptr) override;

	virtual void DrawBox(
		const FVector& Center,
		const FVector& Extent,
		const FQuat& Rotation,
		const FColor& Color,
		uint8 DepthPriority,
		float Thickness = 0.0f) override;

	virtual void DrawPlane(
		const FVector& Center,
		const FVector& Normal,
		const FVector2D& Extents,
		const FColor& Color,
		uint8 DepthPriority) override;

	virtual void DrawCameraFrustum(
		const FComposableCameraTracePose& Pose,
		const FColor& Color,
		uint8 DepthPriority,
		float Scale) override;

private:
	TArray<FComposableCameraDebugPrimitive>& Primitives;
};
