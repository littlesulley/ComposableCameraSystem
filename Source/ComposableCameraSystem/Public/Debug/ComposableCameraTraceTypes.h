// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "SceneManagement.h"

enum class EComposableCameraTraceSourceKind : uint8
{
	CCS_PCM,
	CCS_LevelSequence,
	Native_Camera,
	Unknown,
};

enum class EComposableCameraTraceProjectionStatus : uint8
{
	None,
	ProjectedToPCMCache,
	ProjectedToCineCamera,
	SkippedFramingFailed,
	SkippedMissingOutputComponent,
};

enum class EComposableCameraDebugPrimitiveKind : uint8
{
	Line,
	Point,
	Sphere,
	SolidSphere,
	Box,
	CameraFrustum,
	Plane,
};

struct COMPOSABLECAMERASYSTEM_API FComposableCameraTracePose
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float FieldOfView = 90.0f;
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionMode = ECameraProjectionMode::Perspective;
	float OrthoWidth = 512.0f;
	float OrthoNearClipPlane = 0.0f;
	float OrthoFarClipPlane = 0.0f;
	bool bConstrainAspectRatio = false;

	void Serialize(FArchive& Ar);
};

struct COMPOSABLECAMERASYSTEM_API FComposableCameraDebugPrimitive
{
	EComposableCameraDebugPrimitiveKind Kind = EComposableCameraDebugPrimitiveKind::Line;
	FVector A = FVector::ZeroVector;
	FVector B = FVector::ZeroVector;
	FVector C = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FColor Color = FColor::White;
	float Radius = 0.0f;
	float Size = 0.0f;
	float Thickness = 0.0f;
	uint8 Alpha = 255;
	uint8 DepthPriority = SDPG_World;
	FName Label = NAME_None;

	static FComposableCameraDebugPrimitive MakeLine(
		const FVector& Start,
		const FVector& End,
		const FColor& InColor,
		float InThickness,
		uint8 InDepthPriority);

	static FComposableCameraDebugPrimitive MakePoint(
		const FVector& Location,
		const FColor& InColor,
		float InSize,
		uint8 InDepthPriority);

	static FComposableCameraDebugPrimitive MakeSphere(
		const FVector& Center,
		float InRadius,
		const FColor& InColor,
		uint8 InAlpha,
		uint8 InDepthPriority,
		bool bSolid,
		int32 InSegments = 12,
		float InThickness = 0.0f,
		FName InLabel = NAME_None);

	static FComposableCameraDebugPrimitive MakeBox(
		const FVector& Center,
		const FVector& InExtent,
		const FQuat& InRotation,
		const FColor& InColor,
		uint8 InDepthPriority,
		float InThickness = 0.0f);

	static FComposableCameraDebugPrimitive MakeCameraFrustum(
		const FComposableCameraTracePose& Pose,
		const FColor& InColor,
		uint8 InDepthPriority,
		float InScale = 1.0f);

	static FComposableCameraDebugPrimitive MakePlane(
		const FVector& Center,
		const FVector& Normal,
		const FVector2D& Extents,
		const FColor& InColor,
		uint8 InDepthPriority);

	void Serialize(FArchive& Ar, uint8 StreamVersion);
};

struct COMPOSABLECAMERASYSTEM_API FComposableCameraActiveTraceFrame
{
	uint64 FrameCycle = 0;
	double RecordingTime = 0.0;
	uint64 WorldId = 0;
	uint64 PlayerControllerId = 0;
	uint64 PawnId = 0;
	uint64 PlayerCameraManagerId = 0;
	uint64 ViewTargetActorId = 0;
	uint64 CameraComponentId = 0;
	EComposableCameraTraceSourceKind SourceKind = EComposableCameraTraceSourceKind::Unknown;
	FComposableCameraTracePose RenderedPose;
};

struct COMPOSABLECAMERASYSTEM_API FComposableCameraEvaluationTraceFrame
{
	uint64 FrameCycle = 0;
	double RecordingTime = 0.0;
	uint64 WorldId = 0;
	uint64 SourceObjectId = 0;
	uint64 OwnerPawnId = 0;
	uint64 PlayerControllerId = 0;
	uint64 ViewTargetActorId = 0;
	EComposableCameraTraceSourceKind SourceKind = EComposableCameraTraceSourceKind::Unknown;
	EComposableCameraTraceProjectionStatus ProjectionStatus = EComposableCameraTraceProjectionStatus::None;
	FName CameraTypeAssetName = NAME_None;
	FName ContextName = NAME_None;
	FComposableCameraTracePose CCSPose;
	TArray<FComposableCameraDebugPrimitive> Primitives;
};

COMPOSABLECAMERASYSTEM_API bool SerializeComposableCameraDebugPrimitives(
	const TArray<FComposableCameraDebugPrimitive>& Primitives,
	TArray<uint8>& OutBytes);

COMPOSABLECAMERASYSTEM_API bool DeserializeComposableCameraDebugPrimitives(
	const TArray<uint8>& Bytes,
	TArray<FComposableCameraDebugPrimitive>& OutPrimitives);

COMPOSABLECAMERASYSTEM_API bool DoesComposableCameraEvaluationMatchActiveFrame(
	const FComposableCameraActiveTraceFrame& ActiveFrame,
	const FComposableCameraEvaluationTraceFrame& EvaluationFrame);
