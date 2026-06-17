// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraTraceTypes.h"

#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace
{
	constexpr uint8 GComposableCameraPrimitiveStreamMagic = 0xCC;
	constexpr uint8 GComposableCameraPrimitiveStreamVersion = 1;
	constexpr int32 GComposableCameraMaxPrimitiveStreamCount = 16384;
	constexpr int32 GComposableCameraPrimitiveStreamHeaderBytes = sizeof(uint8) + sizeof(uint8) + sizeof(int32);

	static bool IsValidComposableCameraDebugPrimitiveKind(uint8 Value)
	{
		return Value <= static_cast<uint8>(EComposableCameraDebugPrimitiveKind::CameraFrustum);
	}
}

void FComposableCameraTracePose::Serialize(FArchive& Ar)
{
	Ar << Location;
	Ar << Rotation;
	Ar << FieldOfView;
	Ar << ProjectionMode;
	Ar << OrthoWidth;
	Ar << OrthoNearClipPlane;
	Ar << OrthoFarClipPlane;
	Ar << bConstrainAspectRatio;
}

FComposableCameraDebugPrimitive FComposableCameraDebugPrimitive::MakeLine(
	const FVector& Start,
	const FVector& End,
	const FColor& InColor,
	float InThickness,
	uint8 InDepthPriority)
{
	FComposableCameraDebugPrimitive Primitive;
	Primitive.Kind = EComposableCameraDebugPrimitiveKind::Line;
	Primitive.A = Start;
	Primitive.B = End;
	Primitive.Color = InColor;
	Primitive.Thickness = InThickness;
	Primitive.DepthPriority = InDepthPriority;
	return Primitive;
}

FComposableCameraDebugPrimitive FComposableCameraDebugPrimitive::MakePoint(
	const FVector& Location,
	const FColor& InColor,
	float InSize,
	uint8 InDepthPriority)
{
	FComposableCameraDebugPrimitive Primitive;
	Primitive.Kind = EComposableCameraDebugPrimitiveKind::Point;
	Primitive.A = Location;
	Primitive.Color = InColor;
	Primitive.Size = InSize;
	Primitive.DepthPriority = InDepthPriority;
	return Primitive;
}

FComposableCameraDebugPrimitive FComposableCameraDebugPrimitive::MakeSphere(
	const FVector& Center,
	float InRadius,
	const FColor& InColor,
	uint8 InAlpha,
	uint8 InDepthPriority,
	bool bSolid)
{
	FComposableCameraDebugPrimitive Primitive;
	Primitive.Kind = bSolid
		? EComposableCameraDebugPrimitiveKind::SolidSphere
		: EComposableCameraDebugPrimitiveKind::Sphere;
	Primitive.A = Center;
	Primitive.Radius = InRadius;
	Primitive.Color = InColor;
	Primitive.Alpha = InAlpha;
	Primitive.DepthPriority = InDepthPriority;
	return Primitive;
}

FComposableCameraDebugPrimitive FComposableCameraDebugPrimitive::MakeBox(
	const FVector& Center,
	const FVector& InExtent,
	const FQuat& InRotation,
	const FColor& InColor,
	uint8 InDepthPriority)
{
	FComposableCameraDebugPrimitive Primitive;
	Primitive.Kind = EComposableCameraDebugPrimitiveKind::Box;
	Primitive.A = Center;
	Primitive.Extent = InExtent;
	Primitive.Rotation = InRotation.Rotator();
	Primitive.Color = InColor;
	Primitive.DepthPriority = InDepthPriority;
	return Primitive;
}

FComposableCameraDebugPrimitive FComposableCameraDebugPrimitive::MakeCameraFrustum(
	const FComposableCameraTracePose& Pose,
	const FColor& InColor,
	uint8 InDepthPriority)
{
	FComposableCameraDebugPrimitive Primitive;
	Primitive.Kind = EComposableCameraDebugPrimitiveKind::CameraFrustum;
	Primitive.A = Pose.Location;
	Primitive.Rotation = Pose.Rotation;
	Primitive.Radius = Pose.FieldOfView;
	Primitive.Size = Pose.OrthoWidth;
	Primitive.Color = InColor;
	Primitive.DepthPriority = InDepthPriority;
	return Primitive;
}

void FComposableCameraDebugPrimitive::Serialize(FArchive& Ar)
{
	uint8 KindValue = static_cast<uint8>(Kind);

	Ar << KindValue;
	Ar << A;
	Ar << B;
	Ar << C;
	Ar << Extent;
	Ar << Rotation;
	Ar << Color;
	Ar << Radius;
	Ar << Size;
	Ar << Thickness;
	Ar << Alpha;
	Ar << DepthPriority;

	if (Ar.IsLoading())
	{
		Kind = static_cast<EComposableCameraDebugPrimitiveKind>(KindValue);
	}
}

bool SerializeComposableCameraDebugPrimitives(
	const TArray<FComposableCameraDebugPrimitive>& Primitives,
	TArray<uint8>& OutBytes)
{
	if (Primitives.Num() > GComposableCameraMaxPrimitiveStreamCount)
	{
		OutBytes.Reset();
		return false;
	}

	FBufferArchive Archive;
	uint8 Magic = GComposableCameraPrimitiveStreamMagic;
	uint8 Version = GComposableCameraPrimitiveStreamVersion;
	int32 Count = Primitives.Num();

	Archive << Magic;
	Archive << Version;
	Archive << Count;

	for (FComposableCameraDebugPrimitive Primitive : Primitives)
	{
		Primitive.Serialize(Archive);
	}

	OutBytes = MoveTemp(Archive);
	return true;
}

bool DeserializeComposableCameraDebugPrimitives(
	const TArray<uint8>& Bytes,
	TArray<FComposableCameraDebugPrimitive>& OutPrimitives)
{
	OutPrimitives.Reset();
	if (Bytes.Num() < GComposableCameraPrimitiveStreamHeaderBytes)
	{
		return false;
	}

	FMemoryReader Reader(Bytes);
	uint8 Magic = 0;
	uint8 Version = 0;
	int32 Count = 0;

	Reader << Magic;
	Reader << Version;
	Reader << Count;

	if (Reader.IsError()
		|| Magic != GComposableCameraPrimitiveStreamMagic
		|| Version != GComposableCameraPrimitiveStreamVersion
		|| Count < 0
		|| Count > GComposableCameraMaxPrimitiveStreamCount)
	{
		OutPrimitives.Reset();
		return false;
	}

	OutPrimitives.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FComposableCameraDebugPrimitive Primitive;
		Primitive.Serialize(Reader);
		if (Reader.IsError()
			|| !IsValidComposableCameraDebugPrimitiveKind(static_cast<uint8>(Primitive.Kind)))
		{
			OutPrimitives.Reset();
			return false;
		}
		OutPrimitives.Add(Primitive);
	}

	return true;
}

bool DoesComposableCameraEvaluationMatchActiveFrame(
	const FComposableCameraActiveTraceFrame& ActiveFrame,
	const FComposableCameraEvaluationTraceFrame& EvaluationFrame)
{
	if (ActiveFrame.SourceKind == EComposableCameraTraceSourceKind::CCS_PCM)
	{
		return EvaluationFrame.SourceKind == EComposableCameraTraceSourceKind::CCS_PCM
			&& ActiveFrame.PlayerCameraManagerId != 0
			&& ActiveFrame.PlayerCameraManagerId == EvaluationFrame.SourceObjectId
			&& ActiveFrame.FrameCycle == EvaluationFrame.FrameCycle;
	}

	if (ActiveFrame.SourceKind == EComposableCameraTraceSourceKind::CCS_LevelSequence)
	{
		return EvaluationFrame.SourceKind == EComposableCameraTraceSourceKind::CCS_LevelSequence
			&& ActiveFrame.ViewTargetActorId != 0
			&& ActiveFrame.ViewTargetActorId == EvaluationFrame.ViewTargetActorId;
	}

	return false;
}
