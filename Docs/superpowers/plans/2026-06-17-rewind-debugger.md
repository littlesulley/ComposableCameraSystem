# CCS Rewind Debugger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Rewind Debugger playback for the selected character's historical rendered camera pose plus CCS 3D node and transition gizmos.

**Architecture:** Runtime records two trace streams: ActiveCamera frames from the player camera manager and CCSEvaluation frames from CCS evaluators. Editor trace services ingest both streams, match active frames to CCS frames during Rewind playback, and draw serialized primitive gizmos without touching live runtime objects.

**Tech Stack:** UE 5.6 runtime/editor modules, UE Trace, TraceServices timelines, RewindDebuggerInterface, `UDebugDrawService`, existing CCS camera evaluation and viewport debug code.

---

## Source Path

Use this exact root:

```text
C:/Users/Sulley/Documents/Unreal Projects/UE5_6/Plugins/ComposableCameraSystem
```

Do not edit `Binaries`, `Intermediate`, `Saved`, `Cooked`, `Temp`, engine install directories, packaged output, or host-project copies.

## File Structure

Create runtime files:

- `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTraceTypes.h`
  - Source-kind enums, trace pose structs, active/evaluation frame structs, debug primitive structs, primitive serialization helpers, and pure matching helpers.
- `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTraceTypes.cpp`
  - Serialization, deserialization, and matching helper implementation.
- `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugDrawSink.h`
  - Draw sink interface plus live/capture sink declarations.
- `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugDrawSink.cpp`
  - Live `DrawDebug*` adapter and primitive capture adapter.
- `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTrace.h`
  - Runtime trace channel names, enable checks, and trace writer declarations.
- `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTrace.cpp`
  - UE trace events and trace writer implementation.
- `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`
  - Serialization and matching automation tests.

Modify runtime files:

- `Source/ComposableCameraSystem/ComposableCameraSystem.Build.cs`
  - Add `TraceLog`.
- `Source/ComposableCameraSystem/Public/Cameras/ComposableCameraCameraBase.h`
- `Source/ComposableCameraSystem/Private/Cameras/ComposableCameraCameraBase.cpp`
  - Route camera debug through the sink.
- `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraCameraNodeBase.h`
  - Add sink-based node debug virtual.
- Every node file with `DrawNodeDebug` override:
  - Convert 3D debug drawing to `FComposableCameraDebugDrawSink`.
- `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraTransitionBase.h`
  - Add sink-based transition debug virtual.
- Every transition file with `DrawTransitionDebug` override:
  - Convert 3D debug drawing to `FComposableCameraDebugDrawSink`.
- `Source/ComposableCameraSystem/Public/Core/ComposableCameraEvaluationTree.h`
- `Source/ComposableCameraSystem/Private/Core/ComposableCameraEvaluationTree.cpp`
  - Route transition debug through the sink.
- `Source/ComposableCameraSystem/Public/Core/ComposableCameraPlayerCameraManager.h`
- `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`
  - Emit active-camera and gameplay CCS evaluation traces.
- `Source/ComposableCameraSystem/Public/LevelSequence/ComposableCameraLevelSequenceComponent.h`
- `Source/ComposableCameraSystem/Private/LevelSequence/ComposableCameraLevelSequenceComponent.cpp`
  - Emit LS CCS evaluation trace and projection status.
- `Source/ComposableCameraSystem/Public/Debug/ComposableCameraViewportDebug.h`
- `Source/ComposableCameraSystem/Private/Debug/ComposableCameraViewportDebug.cpp`
  - Use live sink for existing viewport debug path.

Create editor files:

- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.h`
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.cpp`
  - TraceServices provider and timelines.
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceAnalyzer.h`
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceAnalyzer.cpp`
  - UE trace event analyzer.
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceModule.h`
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceModule.cpp`
  - TraceServices module registration.
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.h`
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.cpp`
  - Rewind extension, playback matching, and `UDebugDrawService` drawing.
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerTrack.h`
- `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerTrack.cpp`
  - Character-row track creator.

Modify editor files:

- `Source/ComposableCameraSystemEditor/Public/ComposableCameraSystemEditorModule.h`
- `Source/ComposableCameraSystemEditor/Private/ComposableCameraSystemEditorModule.cpp`
  - Register/unregister trace module, rewind extension, and track creator.

Modify docs:

- `Docs/DesignDoc.md`
- `Docs/TechDoc.md`
- `Docs/ExecutionFlowExamples.md`

---

### Task 1: Trace Types and Tests

**Files:**

- Create: `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTraceTypes.h`
- Create: `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTraceTypes.cpp`
- Create: `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`

- [ ] **Step 1: Write failing serialization and matching tests**

Create `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraTraceTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTracePrimitiveRoundTripTest,
	"ComposableCameraSystem.RewindTrace.PrimitiveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTracePrimitiveRoundTripTest::RunTest(const FString& Parameters)
{
	TArray<FComposableCameraDebugPrimitive> Input;
	Input.Add(FComposableCameraDebugPrimitive::MakeLine(
		FVector(1.0, 2.0, 3.0),
		FVector(4.0, 5.0, 6.0),
		FColor::Red,
		2.0f,
		SDPG_Foreground));
	Input.Add(FComposableCameraDebugPrimitive::MakeSphere(
		FVector(10.0, 20.0, 30.0),
		42.0f,
		FColor::Green,
		96,
		SDPG_Foreground,
		/*bSolid=*/true));

	TArray<uint8> Bytes;
	UTEST_TRUE("Serialize primitives", SerializeComposableCameraDebugPrimitives(Input, Bytes));

	TArray<FComposableCameraDebugPrimitive> Output;
	UTEST_TRUE("Deserialize primitives", DeserializeComposableCameraDebugPrimitives(Bytes, Output));

	UTEST_EQUAL("Primitive count survives", Output.Num(), 2);
	UTEST_EQUAL("First primitive kind survives", Output[0].Kind, EComposableCameraDebugPrimitiveKind::Line);
	UTEST_EQUAL("Line start survives", Output[0].A, FVector(1.0, 2.0, 3.0));
	UTEST_EQUAL("Line end survives", Output[0].B, FVector(4.0, 5.0, 6.0));
	UTEST_EQUAL("Line color survives", Output[0].Color, FColor::Red);
	UTEST_EQUAL("Sphere kind survives", Output[1].Kind, EComposableCameraDebugPrimitiveKind::SolidSphere);
	UTEST_EQUAL("Sphere radius survives", Output[1].Radius, 42.0f);
	UTEST_EQUAL("Sphere alpha survives", Output[1].Alpha, static_cast<uint8>(96));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceMatchGameplayPCMTest,
	"ComposableCameraSystem.RewindTrace.MatchGameplayPCM",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceMatchGameplayPCMTest::RunTest(const FString& Parameters)
{
	FComposableCameraActiveTraceFrame Active;
	Active.SourceKind = EComposableCameraTraceSourceKind::CCS_PCM;
	Active.PlayerCameraManagerId = 101;
	Active.FrameCycle = 700;

	FComposableCameraEvaluationTraceFrame Eval;
	Eval.SourceKind = EComposableCameraTraceSourceKind::CCS_PCM;
	Eval.SourceObjectId = 101;
	Eval.FrameCycle = 700;

	UTEST_TRUE("Gameplay PCM frame matches same PCM id and cycle",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	Eval.FrameCycle = 701;
	UTEST_FALSE("Gameplay PCM frame rejects different cycle",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceMatchLevelSequenceTest,
	"ComposableCameraSystem.RewindTrace.MatchLevelSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceMatchLevelSequenceTest::RunTest(const FString& Parameters)
{
	FComposableCameraActiveTraceFrame Active;
	Active.SourceKind = EComposableCameraTraceSourceKind::CCS_LevelSequence;
	Active.ViewTargetActorId = 202;

	FComposableCameraEvaluationTraceFrame Eval;
	Eval.SourceKind = EComposableCameraTraceSourceKind::CCS_LevelSequence;
	Eval.ViewTargetActorId = 202;

	UTEST_TRUE("Level Sequence frame matches same view target actor",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	Eval.ViewTargetActorId = 303;
	UTEST_FALSE("Level Sequence frame rejects different view target actor",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceRejectsMalformedPrimitiveStreamTest,
	"ComposableCameraSystem.RewindTrace.RejectsMalformedPrimitiveStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceRejectsMalformedPrimitiveStreamTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Bytes;
	Bytes.Add(0x7f);
	Bytes.Add(0x01);
	Bytes.Add(0x02);

	TArray<FComposableCameraDebugPrimitive> Output;
	UTEST_FALSE("Malformed primitive stream fails cleanly",
		DeserializeComposableCameraDebugPrimitives(Bytes, Output));
	UTEST_EQUAL("Malformed primitive stream produces no output", Output.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: Verify tests fail before implementation**

Run inside Rider, Visual Studio Test Explorer, or Unreal Editor Automation:

```text
ComposableCameraSystem.RewindTrace.*
```

Expected before implementation:

```text
Compile fails because Debug/ComposableCameraTraceTypes.h does not exist.
```

Do not run Unreal automation, UBT, `Build.bat`, `RunUBT.bat`, `dotnet`, or `msbuild` from shell.

- [ ] **Step 3: Add trace type declarations**

Create `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTraceTypes.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
		bool bSolid);

	static FComposableCameraDebugPrimitive MakeBox(
		const FVector& Center,
		const FVector& InExtent,
		const FQuat& InRotation,
		const FColor& InColor,
		uint8 InDepthPriority);

	static FComposableCameraDebugPrimitive MakeCameraFrustum(
		const FComposableCameraTracePose& Pose,
		const FColor& InColor,
		uint8 InDepthPriority);

	void Serialize(FArchive& Ar);
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
```

- [ ] **Step 4: Add trace type implementation**

Create `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTraceTypes.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraTraceTypes.h"

#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace
{
	constexpr uint8 GComposableCameraPrimitiveStreamMagic = 0xCC;
	constexpr uint8 GComposableCameraPrimitiveStreamVersion = 1;
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
	Ar << Kind;
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
}

bool SerializeComposableCameraDebugPrimitives(
	const TArray<FComposableCameraDebugPrimitive>& Primitives,
	TArray<uint8>& OutBytes)
{
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
	if (Bytes.Num() < 3)
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
		|| Count > 16384)
	{
		OutPrimitives.Reset();
		return false;
	}

	OutPrimitives.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FComposableCameraDebugPrimitive Primitive;
		Primitive.Serialize(Reader);
		if (Reader.IsError())
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
```

- [ ] **Step 5: Run trace type tests**

Run inside Rider, Visual Studio Test Explorer, or Unreal Editor Automation:

```text
ComposableCameraSystem.RewindTrace.PrimitiveRoundTrip
ComposableCameraSystem.RewindTrace.MatchGameplayPCM
ComposableCameraSystem.RewindTrace.MatchLevelSequence
ComposableCameraSystem.RewindTrace.RejectsMalformedPrimitiveStream
```

Expected after implementation:

```text
All four tests pass.
```

- [ ] **Step 6: Commit trace types**

```bash
git add Source/ComposableCameraSystem/Public/Debug/ComposableCameraTraceTypes.h Source/ComposableCameraSystem/Private/Debug/ComposableCameraTraceTypes.cpp Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp
git commit -m "test: add CCS rewind trace type coverage"
```

---

### Task 2: Runtime Trace Writers

**Files:**

- Modify: `Source/ComposableCameraSystem/ComposableCameraSystem.Build.cs`
- Create: `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTrace.h`
- Create: `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTrace.cpp`

- [ ] **Step 1: Add runtime trace dependency**

In `Source/ComposableCameraSystem/ComposableCameraSystem.Build.cs`, add `"TraceLog"` to `PublicDependencyModuleNames` after `"ApplicationCore"`:

```csharp
                "ApplicationCore",
                "TraceLog"
```

- [ ] **Step 2: Add trace writer header**

Create `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTrace.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"
#include "Trace/Config.h"

#define UE_COMPOSABLE_CAMERA_TRACE (UE_TRACE_ENABLED && !IS_PROGRAM && !UE_BUILD_SHIPPING && !UE_BUILD_TEST)

#if UE_COMPOSABLE_CAMERA_TRACE

class UWorld;

class COMPOSABLECAMERASYSTEM_API FComposableCameraTrace
{
public:
	static FString ChannelName;
	static FString LoggerName;
	static FString ActiveCameraEventName;
	static FString EvaluationEventName;

	static bool IsTraceEnabled();
	static void TraceActiveCamera(UWorld* World, const FComposableCameraActiveTraceFrame& Frame);
	static void TraceEvaluation(UWorld* World, const FComposableCameraEvaluationTraceFrame& Frame);
};

#endif // UE_COMPOSABLE_CAMERA_TRACE
```

- [ ] **Step 3: Add trace writer implementation**

Create `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTrace.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraTrace.h"

#include "ObjectTrace.h"
#include "Serialization/BufferArchive.h"

#if UE_COMPOSABLE_CAMERA_TRACE

bool GComposableCameraDebugTrace = false;
static FAutoConsoleVariableRef CVarComposableCameraDebugTrace(
	TEXT("CCS.Debug.Trace"),
	GComposableCameraDebugTrace,
	TEXT("Enables CCS camera trace events for Rewind Debugger playback."));

UE_TRACE_CHANNEL(ComposableCameraSystemChannel)

UE_TRACE_EVENT_BEGIN(ComposableCameraSystem, ActiveCamera)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, WorldId)
	UE_TRACE_EVENT_FIELD(uint64, PlayerControllerId)
	UE_TRACE_EVENT_FIELD(uint64, PawnId)
	UE_TRACE_EVENT_FIELD(uint64, PlayerCameraManagerId)
	UE_TRACE_EVENT_FIELD(uint64, ViewTargetActorId)
	UE_TRACE_EVENT_FIELD(uint64, CameraComponentId)
	UE_TRACE_EVENT_FIELD(uint8, SourceKind)
	UE_TRACE_EVENT_FIELD(double, LocationX)
	UE_TRACE_EVENT_FIELD(double, LocationY)
	UE_TRACE_EVENT_FIELD(double, LocationZ)
	UE_TRACE_EVENT_FIELD(double, RotationPitch)
	UE_TRACE_EVENT_FIELD(double, RotationYaw)
	UE_TRACE_EVENT_FIELD(double, RotationRoll)
	UE_TRACE_EVENT_FIELD(float, FieldOfView)
	UE_TRACE_EVENT_FIELD(uint8, ProjectionMode)
	UE_TRACE_EVENT_FIELD(float, OrthoWidth)
	UE_TRACE_EVENT_FIELD(float, OrthoNearClipPlane)
	UE_TRACE_EVENT_FIELD(float, OrthoFarClipPlane)
	UE_TRACE_EVENT_FIELD(bool, bConstrainAspectRatio)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(ComposableCameraSystem, CCSEvaluation)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, WorldId)
	UE_TRACE_EVENT_FIELD(uint64, SourceObjectId)
	UE_TRACE_EVENT_FIELD(uint64, OwnerPawnId)
	UE_TRACE_EVENT_FIELD(uint64, PlayerControllerId)
	UE_TRACE_EVENT_FIELD(uint64, ViewTargetActorId)
	UE_TRACE_EVENT_FIELD(uint8, SourceKind)
	UE_TRACE_EVENT_FIELD(uint8, ProjectionStatus)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, CameraTypeAssetName)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, ContextName)
	UE_TRACE_EVENT_FIELD(double, LocationX)
	UE_TRACE_EVENT_FIELD(double, LocationY)
	UE_TRACE_EVENT_FIELD(double, LocationZ)
	UE_TRACE_EVENT_FIELD(double, RotationPitch)
	UE_TRACE_EVENT_FIELD(double, RotationYaw)
	UE_TRACE_EVENT_FIELD(double, RotationRoll)
	UE_TRACE_EVENT_FIELD(float, FieldOfView)
	UE_TRACE_EVENT_FIELD(uint8[], SerializedPrimitives)
UE_TRACE_EVENT_END()

FString FComposableCameraTrace::ChannelName(TEXT("ComposableCameraSystemChannel"));
FString FComposableCameraTrace::LoggerName(TEXT("ComposableCameraSystem"));
FString FComposableCameraTrace::ActiveCameraEventName(TEXT("ActiveCamera"));
FString FComposableCameraTrace::EvaluationEventName(TEXT("CCSEvaluation"));

bool FComposableCameraTrace::IsTraceEnabled()
{
	return GComposableCameraDebugTrace || UE_TRACE_CHANNELEXPR_IS_ENABLED(ComposableCameraSystemChannel);
}

void FComposableCameraTrace::TraceActiveCamera(
	UWorld* World,
	const FComposableCameraActiveTraceFrame& Frame)
{
	if (!IsTraceEnabled())
	{
		return;
	}

	const FComposableCameraTracePose& Pose = Frame.RenderedPose;
	UE_TRACE_LOG(ComposableCameraSystem, ActiveCamera, ComposableCameraSystemChannel)
		<< ActiveCamera.Cycle(Frame.FrameCycle != 0 ? Frame.FrameCycle : FPlatformTime::Cycles64())
		<< ActiveCamera.RecordingTime(Frame.RecordingTime != 0.0 ? Frame.RecordingTime : FObjectTrace::GetWorldElapsedTime(World))
		<< ActiveCamera.WorldId(Frame.WorldId)
		<< ActiveCamera.PlayerControllerId(Frame.PlayerControllerId)
		<< ActiveCamera.PawnId(Frame.PawnId)
		<< ActiveCamera.PlayerCameraManagerId(Frame.PlayerCameraManagerId)
		<< ActiveCamera.ViewTargetActorId(Frame.ViewTargetActorId)
		<< ActiveCamera.CameraComponentId(Frame.CameraComponentId)
		<< ActiveCamera.SourceKind(static_cast<uint8>(Frame.SourceKind))
		<< ActiveCamera.LocationX(Pose.Location.X)
		<< ActiveCamera.LocationY(Pose.Location.Y)
		<< ActiveCamera.LocationZ(Pose.Location.Z)
		<< ActiveCamera.RotationPitch(Pose.Rotation.Pitch)
		<< ActiveCamera.RotationYaw(Pose.Rotation.Yaw)
		<< ActiveCamera.RotationRoll(Pose.Rotation.Roll)
		<< ActiveCamera.FieldOfView(Pose.FieldOfView)
		<< ActiveCamera.ProjectionMode(static_cast<uint8>(Pose.ProjectionMode.GetValue()))
		<< ActiveCamera.OrthoWidth(Pose.OrthoWidth)
		<< ActiveCamera.OrthoNearClipPlane(Pose.OrthoNearClipPlane)
		<< ActiveCamera.OrthoFarClipPlane(Pose.OrthoFarClipPlane)
		<< ActiveCamera.bConstrainAspectRatio(Pose.bConstrainAspectRatio);
}

void FComposableCameraTrace::TraceEvaluation(
	UWorld* World,
	const FComposableCameraEvaluationTraceFrame& Frame)
{
	if (!IsTraceEnabled())
	{
		return;
	}

	TArray<uint8> SerializedPrimitives;
	SerializeComposableCameraDebugPrimitives(Frame.Primitives, SerializedPrimitives);

	const FString CameraTypeAssetString = Frame.CameraTypeAssetName.ToString();
	const FString ContextString = Frame.ContextName.ToString();
	const FComposableCameraTracePose& Pose = Frame.CCSPose;

	UE_TRACE_LOG(ComposableCameraSystem, CCSEvaluation, ComposableCameraSystemChannel)
		<< CCSEvaluation.Cycle(Frame.FrameCycle != 0 ? Frame.FrameCycle : FPlatformTime::Cycles64())
		<< CCSEvaluation.RecordingTime(Frame.RecordingTime != 0.0 ? Frame.RecordingTime : FObjectTrace::GetWorldElapsedTime(World))
		<< CCSEvaluation.WorldId(Frame.WorldId)
		<< CCSEvaluation.SourceObjectId(Frame.SourceObjectId)
		<< CCSEvaluation.OwnerPawnId(Frame.OwnerPawnId)
		<< CCSEvaluation.PlayerControllerId(Frame.PlayerControllerId)
		<< CCSEvaluation.ViewTargetActorId(Frame.ViewTargetActorId)
		<< CCSEvaluation.SourceKind(static_cast<uint8>(Frame.SourceKind))
		<< CCSEvaluation.ProjectionStatus(static_cast<uint8>(Frame.ProjectionStatus))
		<< CCSEvaluation.CameraTypeAssetName(*CameraTypeAssetString, CameraTypeAssetString.Len())
		<< CCSEvaluation.ContextName(*ContextString, ContextString.Len())
		<< CCSEvaluation.LocationX(Pose.Location.X)
		<< CCSEvaluation.LocationY(Pose.Location.Y)
		<< CCSEvaluation.LocationZ(Pose.Location.Z)
		<< CCSEvaluation.RotationPitch(Pose.Rotation.Pitch)
		<< CCSEvaluation.RotationYaw(Pose.Rotation.Yaw)
		<< CCSEvaluation.RotationRoll(Pose.Rotation.Roll)
		<< CCSEvaluation.FieldOfView(Pose.FieldOfView)
		<< CCSEvaluation.SerializedPrimitives(SerializedPrimitives.GetData(), SerializedPrimitives.Num());
}

#endif // UE_COMPOSABLE_CAMERA_TRACE
```

- [ ] **Step 4: Run IDE compile**

Compile in Rider or Visual Studio.

Expected result:

```text
ComposableCameraSystem compiles.
No runtime trace code is compiled in shipping/test builds because UE_COMPOSABLE_CAMERA_TRACE is false there.
```

- [ ] **Step 5: Commit runtime trace writers**

```bash
git add Source/ComposableCameraSystem/ComposableCameraSystem.Build.cs Source/ComposableCameraSystem/Public/Debug/ComposableCameraTrace.h Source/ComposableCameraSystem/Private/Debug/ComposableCameraTrace.cpp
git commit -m "feat: add CCS rewind trace writers"
```

---

### Task 3: Debug Draw Sink

**Files:**

- Create: `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugDrawSink.h`
- Create: `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugDrawSink.cpp`
- Modify: `Source/ComposableCameraSystem/Public/Debug/ComposableCameraViewportDebug.h`
- Modify: `Source/ComposableCameraSystem/Private/Debug/ComposableCameraViewportDebug.cpp`
- Modify: `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`

- [ ] **Step 1: Add sink test**

Append to `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp` before `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceCaptureSinkRecordsPrimitivesTest,
	"ComposableCameraSystem.RewindTrace.CaptureSinkRecordsPrimitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceCaptureSinkRecordsPrimitivesTest::RunTest(const FString& Parameters)
{
	TArray<FComposableCameraDebugPrimitive> Primitives;
	FComposableCameraPrimitiveCaptureSink Sink(Primitives);

	Sink.DrawLine(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), FColor::Blue, 3.0f, SDPG_Foreground);
	Sink.DrawPoint(FVector(2.0, 0.0, 0.0), FColor::Red, 5.0f, SDPG_World);
	Sink.DrawSphere(FVector(3.0, 0.0, 0.0), 9.0f, FColor::Green, 80, SDPG_Foreground, true);

	UTEST_EQUAL("Capture sink recorded three primitives", Primitives.Num(), 3);
	UTEST_EQUAL("First primitive line", Primitives[0].Kind, EComposableCameraDebugPrimitiveKind::Line);
	UTEST_EQUAL("Second primitive point", Primitives[1].Kind, EComposableCameraDebugPrimitiveKind::Point);
	UTEST_EQUAL("Third primitive solid sphere", Primitives[2].Kind, EComposableCameraDebugPrimitiveKind::SolidSphere);

	return true;
}
```

Add include near the top:

```cpp
#include "Debug/ComposableCameraDebugDrawSink.h"
```

- [ ] **Step 2: Create sink header**

Create `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugDrawSink.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"

class UWorld;

class COMPOSABLECAMERASYSTEM_API FComposableCameraDebugDrawSink
{
public:
	virtual ~FComposableCameraDebugDrawSink() = default;

	virtual void DrawLine(const FVector& Start, const FVector& End, const FColor& Color, float Thickness, uint8 DepthPriority) = 0;
	virtual void DrawPoint(const FVector& Location, const FColor& Color, float Size, uint8 DepthPriority) = 0;
	virtual void DrawSphere(const FVector& Center, float Radius, const FColor& Color, uint8 Alpha, uint8 DepthPriority, bool bSolid) = 0;
	virtual void DrawBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, uint8 DepthPriority) = 0;
	virtual void DrawCameraFrustum(const FComposableCameraTracePose& Pose, const FColor& Color, uint8 DepthPriority) = 0;
};

class COMPOSABLECAMERASYSTEM_API FComposableCameraLiveDebugDrawSink final : public FComposableCameraDebugDrawSink
{
public:
	explicit FComposableCameraLiveDebugDrawSink(UWorld* InWorld);

	virtual void DrawLine(const FVector& Start, const FVector& End, const FColor& Color, float Thickness, uint8 DepthPriority) override;
	virtual void DrawPoint(const FVector& Location, const FColor& Color, float Size, uint8 DepthPriority) override;
	virtual void DrawSphere(const FVector& Center, float Radius, const FColor& Color, uint8 Alpha, uint8 DepthPriority, bool bSolid) override;
	virtual void DrawBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, uint8 DepthPriority) override;
	virtual void DrawCameraFrustum(const FComposableCameraTracePose& Pose, const FColor& Color, uint8 DepthPriority) override;

private:
	TObjectPtr<UWorld> World = nullptr;
};

class COMPOSABLECAMERASYSTEM_API FComposableCameraPrimitiveCaptureSink final : public FComposableCameraDebugDrawSink
{
public:
	explicit FComposableCameraPrimitiveCaptureSink(TArray<FComposableCameraDebugPrimitive>& InPrimitives);

	virtual void DrawLine(const FVector& Start, const FVector& End, const FColor& Color, float Thickness, uint8 DepthPriority) override;
	virtual void DrawPoint(const FVector& Location, const FColor& Color, float Size, uint8 DepthPriority) override;
	virtual void DrawSphere(const FVector& Center, float Radius, const FColor& Color, uint8 Alpha, uint8 DepthPriority, bool bSolid) override;
	virtual void DrawBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, uint8 DepthPriority) override;
	virtual void DrawCameraFrustum(const FComposableCameraTracePose& Pose, const FColor& Color, uint8 DepthPriority) override;

private:
	TArray<FComposableCameraDebugPrimitive>& Primitives;
};
```

- [ ] **Step 3: Create sink implementation**

Create `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugDrawSink.cpp`:

```cpp
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
	if (World)
	{
		DrawDebugLine(World, Start, End, Color, false, -1.0f, DepthPriority, Thickness);
	}
}

void FComposableCameraLiveDebugDrawSink::DrawPoint(
	const FVector& Location,
	const FColor& Color,
	float Size,
	uint8 DepthPriority)
{
	if (World)
	{
		DrawDebugPoint(World, Location, Size, Color, false, -1.0f, DepthPriority);
	}
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

	if (bSolid)
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(World, Center, Radius, Color, Alpha, 12, DepthPriority);
	}
	else
	{
		DrawDebugSphere(World, Center, Radius, 12, FColor(Color.R, Color.G, Color.B, Alpha), false, -1.0f, DepthPriority);
	}
}

void FComposableCameraLiveDebugDrawSink::DrawBox(
	const FVector& Center,
	const FVector& Extent,
	const FQuat& Rotation,
	const FColor& Color,
	uint8 DepthPriority)
{
	if (World)
	{
		DrawDebugBox(World, Center, Extent, Rotation, Color, false, -1.0f, DepthPriority);
	}
}

void FComposableCameraLiveDebugDrawSink::DrawCameraFrustum(
	const FComposableCameraTracePose& Pose,
	const FColor& Color,
	uint8 DepthPriority)
{
	if (World)
	{
		DrawDebugCamera(World, Pose.Location, Pose.Rotation, Pose.FieldOfView, 100.0f, Color, false, -1.0f, DepthPriority);
	}
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
```

- [ ] **Step 4: Keep `DrawSolidDebugSphere` API**

Leave `FComposableCameraViewportDebug::DrawSolidDebugSphere` in place for live drawing and tests. The sink calls it for live solid spheres. Do not remove the old API in this task.

- [ ] **Step 5: Run sink tests**

Run inside Rider, Visual Studio Test Explorer, or Unreal Editor Automation:

```text
ComposableCameraSystem.RewindTrace.CaptureSinkRecordsPrimitives
```

Expected:

```text
Capture sink records line, point, and solid sphere primitives.
```

- [ ] **Step 6: Commit draw sink**

```bash
git add Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugDrawSink.h Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugDrawSink.cpp Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp
git commit -m "feat: add CCS debug primitive draw sink"
```

---

### Task 4: Convert Camera, Node, and Transition Gizmos to Sink

**Files:**

- Modify: `Source/ComposableCameraSystem/Public/Cameras/ComposableCameraCameraBase.h`
- Modify: `Source/ComposableCameraSystem/Private/Cameras/ComposableCameraCameraBase.cpp`
- Modify: `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraCameraNodeBase.h`
- Modify: every `Source/ComposableCameraSystem/Public/Nodes/*Node.h` with `DrawNodeDebug`
- Modify: every `Source/ComposableCameraSystem/Private/Nodes/*Node.cpp` with `DrawNodeDebug`
- Modify: `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraTransitionBase.h`
- Modify: every `Source/ComposableCameraSystem/Public/Transitions/*Transition.h` with `DrawTransitionDebug`
- Modify: every `Source/ComposableCameraSystem/Private/Transitions/*Transition.cpp` with `DrawTransitionDebug`
- Modify: `Source/ComposableCameraSystem/Public/Core/ComposableCameraEvaluationTree.h`
- Modify: `Source/ComposableCameraSystem/Private/Core/ComposableCameraEvaluationTree.cpp`

- [ ] **Step 1: Add sink-based virtuals**

In `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraCameraNodeBase.h`, include the sink and replace the 3D debug virtual:

```cpp
#include "Debug/ComposableCameraDebugDrawSink.h"
```

Use this virtual:

```cpp
virtual void DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const {}
```

In `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraTransitionBase.h`, include the sink and replace the 3D debug virtual:

```cpp
#include "Debug/ComposableCameraDebugDrawSink.h"
```

Use this virtual:

```cpp
virtual void DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const {}
```

- [ ] **Step 2: Update camera debug entry**

In `Source/ComposableCameraSystem/Public/Cameras/ComposableCameraCameraBase.h`, add a sink overload:

```cpp
void DrawCameraDebug(FComposableCameraDebugDrawSink& Draw, bool bDrawFrustum) const;
void DrawCameraDebug(class UWorld* World, bool bDrawFrustum) const;
```

In `Source/ComposableCameraSystem/Private/Cameras/ComposableCameraCameraBase.cpp`, keep the world overload as live adapter:

```cpp
void AComposableCameraCameraBase::DrawCameraDebug(UWorld* World, bool bDrawFrustum) const
{
	FComposableCameraLiveDebugDrawSink Draw(World);
	DrawCameraDebug(Draw, bDrawFrustum);
}

void AComposableCameraCameraBase::DrawCameraDebug(FComposableCameraDebugDrawSink& Draw, bool bDrawFrustum) const
{
	if (bDrawFrustum)
	{
		FComposableCameraTracePose Pose;
		Pose.Location = CameraPose.Position;
		Pose.Rotation = CameraPose.Rotation;
		Pose.FieldOfView = CameraPose.GetEffectiveFieldOfView();
		Pose.ProjectionMode = CameraPose.ProjectionMode;
		Pose.bConstrainAspectRatio = CameraPose.ConstrainAspectRatio;
		Pose.OrthoWidth = CameraPose.OrthographicWidth;
		Pose.OrthoNearClipPlane = CameraPose.OrthoNearClipPlane;
		Pose.OrthoFarClipPlane = CameraPose.OrthoFarClipPlane;
		Draw.DrawCameraFrustum(Pose, FColor::Yellow, SDPG_Foreground);
	}

	for (const UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->DrawNodeDebug(Draw, /*bViewerIsOutsideCamera=*/bDrawFrustum);
		}
	}
}
```

- [ ] **Step 3: Update evaluation-tree transition debug entry**

In `Source/ComposableCameraSystem/Public/Core/ComposableCameraEvaluationTree.h`, add:

```cpp
void DrawTransitionsDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const;
void DrawTransitionsDebug(class UWorld* World, bool bViewerIsOutsideCamera) const;
```

In `Source/ComposableCameraSystem/Private/Core/ComposableCameraEvaluationTree.cpp`, keep the world overload as live adapter:

```cpp
void UComposableCameraEvaluationTree::DrawTransitionsDebug(UWorld* World, bool bViewerIsOutsideCamera) const
{
	FComposableCameraLiveDebugDrawSink Draw(World);
	DrawTransitionsDebug(Draw, bViewerIsOutsideCamera);
}
```

Move the existing traversal body into:

```cpp
void UComposableCameraEvaluationTree::DrawTransitionsDebug(
	FComposableCameraDebugDrawSink& Draw,
	bool bViewerIsOutsideCamera) const
{
	// Existing traversal remains the same.
	// Replace Inner.Transition->DrawTransitionDebug(World, bViewerIsOutsideCamera)
	// with:
	// Inner.Transition->DrawTransitionDebug(Draw, bViewerIsOutsideCamera);
}
```

- [ ] **Step 4: Convert one small node first**

Convert `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraPivotOffsetNode.h` from:

```cpp
virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
```

to:

```cpp
virtual void DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const override;
```

Convert `Source/ComposableCameraSystem/Private/Nodes/ComposableCameraPivotOffsetNode.cpp` from `DrawDebug*` / `FComposableCameraViewportDebug::DrawSolidDebugSphere` calls to sink calls:

```cpp
void UComposableCameraPivotOffsetNode::DrawNodeDebug(
	FComposableCameraDebugDrawSink& Draw,
	bool bViewerIsOutsideCamera) const
{
	if (CVarShowPivotOffsetGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos())
	{
		return;
	}

	Draw.DrawSphere(
		LastPivotLocation,
		16.0f,
		FComposableCameraViewportDebugColors::PivotOffset(),
		100,
		SDPG_Foreground,
		/*bSolid=*/true);
}
```

Use the real cached field names from the file. Preserve each node's CVar guard and current colors.

- [ ] **Step 5: Convert remaining node overrides**

For each file returned by:

```text
rg -n "DrawNodeDebug\\(" Source/ComposableCameraSystem/Public/Nodes Source/ComposableCameraSystem/Private/Nodes
```

apply this exact rule:

```text
Old signature:
DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera)

New signature:
DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera)
```

Replace draw calls:

```text
DrawDebugLine(World, Start, End, Color, false, -1.0f, DepthPriority, Thickness)
-> Draw.DrawLine(Start, End, Color, Thickness, DepthPriority)

DrawDebugPoint(World, Location, Size, Color, false, -1.0f, DepthPriority)
-> Draw.DrawPoint(Location, Color, Size, DepthPriority)

DrawDebugSphere(World, Center, Radius, Segments, Color, false, -1.0f, DepthPriority, Thickness)
-> Draw.DrawSphere(Center, Radius, Color, Color.A, DepthPriority, false)

FComposableCameraViewportDebug::DrawSolidDebugSphere(World, Center, Radius, Color, Alpha, Segments, DepthPriority, Label)
-> Draw.DrawSphere(Center, Radius, Color, Alpha, DepthPriority, true)

DrawDebugBox(World, Center, Extent, Rotation, Color, false, -1.0f, DepthPriority)
-> Draw.DrawBox(Center, Extent, Rotation, Color, DepthPriority)
```

Text labels are excluded from first implementation. Keep the visual primitive and drop the label argument during conversion.

- [ ] **Step 6: Convert transition overrides**

For each file returned by:

```text
rg -n "DrawTransitionDebug\\(" Source/ComposableCameraSystem/Public/Transitions Source/ComposableCameraSystem/Private/Transitions
```

apply this rule:

```text
Old signature:
DrawTransitionDebug(UWorld* World, bool bViewerIsOutsideCamera)

New signature:
DrawTransitionDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera)
```

Use the same draw-call replacement table from Step 5. Preserve each transition CVar guard and color.

- [ ] **Step 7: Validate no direct 3D draw calls remain in node/transition debug overrides**

Run from shell:

```powershell
rg -n "DrawDebug(Line|Point|Sphere|Box|Camera)|DrawSolidDebugSphere" Source/ComposableCameraSystem/Private/Nodes Source/ComposableCameraSystem/Private/Transitions
```

Expected:

```text
No matches in DrawNodeDebug or DrawTransitionDebug bodies.
```

Matches in comments or non-debug runtime logic require manual review. Keep real runtime draw logic out of node/transition debug overrides.

- [ ] **Step 8: Run IDE compile**

Compile in Rider or Visual Studio.

Expected:

```text
All node and transition overrides compile against the sink signatures.
Existing live viewport debug still draws with CCS.Debug.Viewport.
```

- [ ] **Step 9: Commit sink conversion**

```bash
git add Source/ComposableCameraSystem/Public/Cameras Source/ComposableCameraSystem/Private/Cameras Source/ComposableCameraSystem/Public/Nodes Source/ComposableCameraSystem/Private/Nodes Source/ComposableCameraSystem/Public/Transitions Source/ComposableCameraSystem/Private/Transitions Source/ComposableCameraSystem/Public/Core Source/ComposableCameraSystem/Private/Core Source/ComposableCameraSystem/Public/Debug Source/ComposableCameraSystem/Private/Debug
git commit -m "refactor: route CCS 3D gizmos through traceable draw sink"
```

---

### Task 5: Gameplay PCM Trace Capture

**Files:**

- Modify: `Source/ComposableCameraSystem/Public/Core/ComposableCameraPlayerCameraManager.h`
- Modify: `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`

- [ ] **Step 1: Add private trace helpers**

In `AComposableCameraPlayerCameraManager` private section, add:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
void TraceCCSEvaluationFrame(
	const FComposableCameraPose& Pose,
	EComposableCameraTraceProjectionStatus ProjectionStatus,
	uint64 FrameCycle);

void TraceActiveCameraFrame(
	const FMinimalViewInfo& RenderedView,
	EComposableCameraTraceSourceKind SourceKind,
	uint64 FrameCycle);
#endif
```

Include:

```cpp
#include "Debug/ComposableCameraTrace.h"
```

- [ ] **Step 2: Add pose conversion helper in cpp**

In `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`, add local helpers near the top:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
static FComposableCameraTracePose MakeTracePoseFromCCSPose(const FComposableCameraPose& Pose)
{
	FComposableCameraTracePose TracePose;
	TracePose.Location = Pose.Position;
	TracePose.Rotation = Pose.Rotation;
	TracePose.FieldOfView = Pose.GetEffectiveFieldOfView();
	TracePose.ProjectionMode = Pose.ProjectionMode;
	TracePose.bConstrainAspectRatio = Pose.ConstrainAspectRatio;
	TracePose.OrthoWidth = Pose.OrthographicWidth;
	TracePose.OrthoNearClipPlane = Pose.OrthoNearClipPlane;
	TracePose.OrthoFarClipPlane = Pose.OrthoFarClipPlane;
	return TracePose;
}

static FComposableCameraTracePose MakeTracePoseFromMinimalView(const FMinimalViewInfo& View)
{
	FComposableCameraTracePose TracePose;
	TracePose.Location = View.Location;
	TracePose.Rotation = View.Rotation;
	TracePose.FieldOfView = View.FOV;
	TracePose.ProjectionMode = View.ProjectionMode;
	TracePose.bConstrainAspectRatio = View.bConstrainAspectRatio;
	TracePose.OrthoWidth = View.OrthoWidth;
	TracePose.OrthoNearClipPlane = View.OrthoNearClipPlane;
	TracePose.OrthoFarClipPlane = View.OrthoFarClipPlane;
	return TracePose;
}
#endif
```

- [ ] **Step 3: Implement gameplay CCS evaluation trace**

Add implementation:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
void AComposableCameraPlayerCameraManager::TraceCCSEvaluationFrame(
	const FComposableCameraPose& Pose,
	EComposableCameraTraceProjectionStatus ProjectionStatus,
	uint64 FrameCycle)
{
	if (!FComposableCameraTrace::IsTraceEnabled())
	{
		return;
	}

	FComposableCameraEvaluationTraceFrame Frame;
	Frame.FrameCycle = FrameCycle;
	Frame.SourceKind = EComposableCameraTraceSourceKind::CCS_PCM;
	Frame.ProjectionStatus = ProjectionStatus;
	Frame.SourceObjectId = FObjectTrace::GetObjectId(this);
	Frame.WorldId = FObjectTrace::GetObjectId(GetWorld());
	Frame.CCSPose = MakeTracePoseFromCCSPose(Pose);
	Frame.ContextName = CurrentContext;

	if (APlayerController* PC = GetOwningPlayerController())
	{
		Frame.PlayerControllerId = FObjectTrace::GetObjectId(PC);
		if (APawn* Pawn = PC->GetPawn())
		{
			Frame.OwnerPawnId = FObjectTrace::GetObjectId(Pawn);
		}
	}

	if (AActor* ViewTargetActor = GetViewTarget())
	{
		Frame.ViewTargetActorId = FObjectTrace::GetObjectId(ViewTargetActor);
	}

	if (RunningCamera)
	{
		Frame.CameraTypeAssetName = RunningCamera->GetFName();
		Frame.Primitives.Reserve(128);
		FComposableCameraPrimitiveCaptureSink Capture(Frame.Primitives);
		RunningCamera->DrawCameraDebug(Capture, /*bDrawFrustum=*/true);
		if (ContextStack)
		{
			if (UComposableCameraDirector* ActiveDirector = ContextStack->GetActiveDirector())
			{
				if (UComposableCameraEvaluationTree* Tree = ActiveDirector->GetEvaluationTree())
				{
					Tree->DrawTransitionsDebug(Capture, /*bViewerIsOutsideCamera=*/true);
				}
			}
		}
	}

	FComposableCameraTrace::TraceEvaluation(GetWorld(), Frame);
}
#endif
```

Include `ObjectTrace.h`, `Debug/ComposableCameraDebugDrawSink.h`, and director/tree headers already used in this cpp.

- [ ] **Step 4: Implement active-camera trace**

Add implementation:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
void AComposableCameraPlayerCameraManager::TraceActiveCameraFrame(
	const FMinimalViewInfo& RenderedView,
	EComposableCameraTraceSourceKind SourceKind,
	uint64 FrameCycle)
{
	if (!FComposableCameraTrace::IsTraceEnabled())
	{
		return;
	}

	FComposableCameraActiveTraceFrame Frame;
	Frame.FrameCycle = FrameCycle;
	Frame.SourceKind = SourceKind;
	Frame.RenderedPose = MakeTracePoseFromMinimalView(RenderedView);
	Frame.WorldId = FObjectTrace::GetObjectId(GetWorld());
	Frame.PlayerCameraManagerId = FObjectTrace::GetObjectId(this);

	if (APlayerController* PC = GetOwningPlayerController())
	{
		Frame.PlayerControllerId = FObjectTrace::GetObjectId(PC);
		if (APawn* Pawn = PC->GetPawn())
		{
			Frame.PawnId = FObjectTrace::GetObjectId(Pawn);
		}
	}

	if (AActor* ViewTargetActor = GetViewTarget())
	{
		Frame.ViewTargetActorId = FObjectTrace::GetObjectId(ViewTargetActor);
		if (UCameraComponent* CameraComponent = ViewTargetActor->FindComponentByClass<UCameraComponent>())
		{
			Frame.CameraComponentId = FObjectTrace::GetObjectId(CameraComponent);
		}
	}

	FComposableCameraTrace::TraceActiveCamera(GetWorld(), Frame);
}
#endif
```

- [ ] **Step 5: Call trace helpers from `DoUpdateCamera`**

In `DoUpdateCamera`, create one cycle at the start:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
	const uint64 RewindTraceFrameCycle = FPlatformTime::Cycles64();
#endif
```

After `FMinimalViewInfo DesiredView = GetCameraViewFromCameraPose(OutPose);` and after `RunningCamera` / `CurrentContext` are set, call:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
		TraceCCSEvaluationFrame(
			OutPose,
			EComposableCameraTraceProjectionStatus::ProjectedToPCMCache,
			RewindTraceFrameCycle);
#endif
```

After `FillCameraCache(DesiredView);`, call:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
		TraceActiveCameraFrame(
			DesiredView,
			RunningCamera
				? EComposableCameraTraceSourceKind::CCS_PCM
				: EComposableCameraTraceSourceKind::Unknown,
			RewindTraceFrameCycle);
#endif
```

- [ ] **Step 6: Run IDE compile and live debug smoke check**

Compile in Rider or Visual Studio.

Manual smoke check in Unreal:

```text
Enable CCS.Debug.Viewport 1 plus one node CVar.
Start PIE.
Confirm live viewport gizmos still draw.
```

Expected:

```text
Live debug behavior unchanged.
Trace disabled path does not build primitive arrays.
```

- [ ] **Step 7: Commit PCM trace capture**

```bash
git add Source/ComposableCameraSystem/Public/Core/ComposableCameraPlayerCameraManager.h Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp
git commit -m "feat: record gameplay CCS rewind trace frames"
```

---

### Task 6: Level Sequence Evaluation Trace Capture

**Files:**

- Modify: `Source/ComposableCameraSystem/Public/LevelSequence/ComposableCameraLevelSequenceComponent.h`
- Modify: `Source/ComposableCameraSystem/Private/LevelSequence/ComposableCameraLevelSequenceComponent.cpp`

- [ ] **Step 1: Change projection function to return status**

In the header, change:

```cpp
void ProjectPoseToCineCamera(const FComposableCameraPose& Pose);
```

to:

```cpp
EComposableCameraTraceProjectionStatus ProjectPoseToCineCamera(const FComposableCameraPose& Pose);
```

Add include:

```cpp
#include "Debug/ComposableCameraTraceTypes.h"
```

- [ ] **Step 2: Return projection statuses**

In `ProjectPoseToCineCamera`, return explicit statuses:

```cpp
if (!OutputCineCameraComponent)
{
	return EComposableCameraTraceProjectionStatus::SkippedMissingOutputComponent;
}

if (InternalCamera && InternalCamera->bLastTickFramingFailed)
{
	return EComposableCameraTraceProjectionStatus::SkippedFramingFailed;
}
```

At the end of the function, return:

```cpp
return EComposableCameraTraceProjectionStatus::ProjectedToCineCamera;
```

- [ ] **Step 3: Add LS trace helper declaration**

In the component private section, add:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
void TraceLevelSequenceEvaluationFrame(
	const FComposableCameraPose& Pose,
	EComposableCameraTraceProjectionStatus ProjectionStatus,
	uint64 FrameCycle);
#endif
```

Include:

```cpp
#include "Debug/ComposableCameraTrace.h"
```

- [ ] **Step 4: Implement LS trace helper**

In `ComposableCameraLevelSequenceComponent.cpp`, add:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
void UComposableCameraLevelSequenceComponent::TraceLevelSequenceEvaluationFrame(
	const FComposableCameraPose& Pose,
	EComposableCameraTraceProjectionStatus ProjectionStatus,
	uint64 FrameCycle)
{
	if (!FComposableCameraTrace::IsTraceEnabled())
	{
		return;
	}

	FComposableCameraEvaluationTraceFrame Frame;
	Frame.FrameCycle = FrameCycle;
	Frame.SourceKind = EComposableCameraTraceSourceKind::CCS_LevelSequence;
	Frame.ProjectionStatus = ProjectionStatus;
	Frame.WorldId = FObjectTrace::GetObjectId(GetWorld());
	Frame.SourceObjectId = FObjectTrace::GetObjectId(this);
	Frame.ViewTargetActorId = GetOwner() ? FObjectTrace::GetObjectId(GetOwner()) : 0;
	Frame.CameraTypeAssetName = TypeAssetReference.TypeAsset
		? TypeAssetReference.TypeAsset->GetFName()
		: NAME_None;

	Frame.CCSPose.Location = Pose.Position;
	Frame.CCSPose.Rotation = Pose.Rotation;
	Frame.CCSPose.FieldOfView = Pose.GetEffectiveFieldOfView();
	Frame.CCSPose.ProjectionMode = Pose.ProjectionMode;
	Frame.CCSPose.bConstrainAspectRatio = Pose.ConstrainAspectRatio;
	Frame.CCSPose.OrthoWidth = Pose.OrthographicWidth;
	Frame.CCSPose.OrthoNearClipPlane = Pose.OrthoNearClipPlane;
	Frame.CCSPose.OrthoFarClipPlane = Pose.OrthoFarClipPlane;

	if (InternalCamera)
	{
		Frame.Primitives.Reserve(128);
		FComposableCameraPrimitiveCaptureSink Capture(Frame.Primitives);
		InternalCamera->DrawCameraDebug(Capture, /*bDrawFrustum=*/true);
	}

	FComposableCameraTrace::TraceEvaluation(GetWorld(), Frame);
}
#endif
```

Add includes:

```cpp
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "ObjectTrace.h"
```

- [ ] **Step 5: Call LS trace from `EvaluateOnce`**

In `EvaluateOnce`, create a trace cycle before ticking:

```cpp
#if UE_COMPOSABLE_CAMERA_TRACE
	const uint64 RewindTraceFrameCycle = FPlatformTime::Cycles64();
#endif
```

Replace:

```cpp
ProjectPoseToCineCamera(Pose);
```

with:

```cpp
const EComposableCameraTraceProjectionStatus ProjectionStatus = ProjectPoseToCineCamera(Pose);

#if UE_COMPOSABLE_CAMERA_TRACE
TraceLevelSequenceEvaluationFrame(Pose, ProjectionStatus, RewindTraceFrameCycle);
#endif
```

- [ ] **Step 6: Run IDE compile and LS smoke check**

Compile in Rider or Visual Studio.

Manual smoke check in Unreal:

```text
Play a Level Sequence containing AComposableCameraLevelSequenceActor.
Confirm the CineCamera still receives pose.
Confirm framing-failed behavior still holds current CineCamera transform.
```

Expected:

```text
LS behavior unchanged.
Trace disabled path does not collect primitives.
```

- [ ] **Step 7: Commit LS trace capture**

```bash
git add Source/ComposableCameraSystem/Public/LevelSequence/ComposableCameraLevelSequenceComponent.h Source/ComposableCameraSystem/Private/LevelSequence/ComposableCameraLevelSequenceComponent.cpp
git commit -m "feat: record Level Sequence CCS rewind trace frames"
```

---

### Task 7: Editor Trace Provider, Analyzer, and Module Registration

**Files:**

- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.cpp`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceAnalyzer.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceAnalyzer.cpp`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceModule.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceModule.cpp`
- Modify: `Source/ComposableCameraSystemEditor/Public/ComposableCameraSystemEditorModule.h`
- Modify: `Source/ComposableCameraSystemEditor/Private/ComposableCameraSystemEditorModule.cpp`

- [ ] **Step 1: Add provider header**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"
#include "Model/PointTimeline.h"
#include "TraceServices/Model/AnalysisSession.h"

class FComposableCameraTraceProvider : public TraceServices::IProvider
{
public:
	static FName ProviderName;

	explicit FComposableCameraTraceProvider(TraceServices::IAnalysisSession& InSession);

	void AppendActiveFrame(double EventTime, FComposableCameraActiveTraceFrame&& Frame);
	void AppendEvaluationFrame(double EventTime, FComposableCameraEvaluationTraceFrame&& Frame);

	const TraceServices::ITimeline<FComposableCameraActiveTraceFrame>* GetActiveTimeline() const
	{
		return ActiveTimeline.Get();
	}

	const TraceServices::ITimeline<FComposableCameraEvaluationTraceFrame>* GetEvaluationTimeline() const
	{
		return EvaluationTimeline.Get();
	}

private:
	TraceServices::IAnalysisSession& Session;
	TSharedPtr<TraceServices::TPointTimeline<FComposableCameraActiveTraceFrame>> ActiveTimeline;
	TSharedPtr<TraceServices::TPointTimeline<FComposableCameraEvaluationTraceFrame>> EvaluationTimeline;
};
```

- [ ] **Step 2: Add provider implementation**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraTraceProvider.h"

FName FComposableCameraTraceProvider::ProviderName(TEXT("ComposableCameraSystemTraceProvider"));

FComposableCameraTraceProvider::FComposableCameraTraceProvider(
	TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
{
	ActiveTimeline = MakeShared<TraceServices::TPointTimeline<FComposableCameraActiveTraceFrame>>(Session.GetLinearAllocator());
	EvaluationTimeline = MakeShared<TraceServices::TPointTimeline<FComposableCameraEvaluationTraceFrame>>(Session.GetLinearAllocator());
}

void FComposableCameraTraceProvider::AppendActiveFrame(
	double EventTime,
	FComposableCameraActiveTraceFrame&& Frame)
{
	ActiveTimeline->AppendEvent(EventTime, MoveTemp(Frame));
}

void FComposableCameraTraceProvider::AppendEvaluationFrame(
	double EventTime,
	FComposableCameraEvaluationTraceFrame&& Frame)
{
	EvaluationTimeline->AppendEvent(EventTime, MoveTemp(Frame));
}
```

- [ ] **Step 3: Add analyzer**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceAnalyzer.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/AnalysisSession.h"

class FComposableCameraTraceProvider;

class FComposableCameraTraceAnalyzer : public UE::Trace::IAnalyzer
{
public:
	FComposableCameraTraceAnalyzer(
		TraceServices::IAnalysisSession& InSession,
		FComposableCameraTraceProvider& InProvider);

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteActiveCamera = 0,
		RouteCCSEvaluation = 1,
	};

	TraceServices::IAnalysisSession& Session;
	FComposableCameraTraceProvider& Provider;
};
```

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceAnalyzer.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraTraceAnalyzer.h"

#include "Debug/ComposableCameraTrace.h"
#include "Trace/ComposableCameraTraceProvider.h"

FComposableCameraTraceAnalyzer::FComposableCameraTraceAnalyzer(
	TraceServices::IAnalysisSession& InSession,
	FComposableCameraTraceProvider& InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
}

void FComposableCameraTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	Context.InterfaceBuilder.RouteEvent(RouteActiveCamera, "ComposableCameraSystem", "ActiveCamera");
	Context.InterfaceBuilder.RouteEvent(RouteCCSEvaluation, "ComposableCameraSystem", "CCSEvaluation");
}

bool FComposableCameraTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	TraceServices::FAnalysisSessionEditScope EditScope(Session);

	const uint64 Cycle = Context.EventData.GetValue<uint64>("Cycle");
	const double EventTime = Context.EventTime.AsSeconds(Cycle);
	const double RecordingTime = Context.EventData.GetValue<double>("RecordingTime");

	if (RouteId == RouteActiveCamera)
	{
		FComposableCameraActiveTraceFrame Frame;
		Frame.FrameCycle = Cycle;
		Frame.RecordingTime = RecordingTime;
		Frame.WorldId = Context.EventData.GetValue<uint64>("WorldId");
		Frame.PlayerControllerId = Context.EventData.GetValue<uint64>("PlayerControllerId");
		Frame.PawnId = Context.EventData.GetValue<uint64>("PawnId");
		Frame.PlayerCameraManagerId = Context.EventData.GetValue<uint64>("PlayerCameraManagerId");
		Frame.ViewTargetActorId = Context.EventData.GetValue<uint64>("ViewTargetActorId");
		Frame.CameraComponentId = Context.EventData.GetValue<uint64>("CameraComponentId");
		Frame.SourceKind = static_cast<EComposableCameraTraceSourceKind>(Context.EventData.GetValue<uint8>("SourceKind"));
		Frame.RenderedPose.Location.X = Context.EventData.GetValue<double>("LocationX");
		Frame.RenderedPose.Location.Y = Context.EventData.GetValue<double>("LocationY");
		Frame.RenderedPose.Location.Z = Context.EventData.GetValue<double>("LocationZ");
		Frame.RenderedPose.Rotation.Pitch = Context.EventData.GetValue<double>("RotationPitch");
		Frame.RenderedPose.Rotation.Yaw = Context.EventData.GetValue<double>("RotationYaw");
		Frame.RenderedPose.Rotation.Roll = Context.EventData.GetValue<double>("RotationRoll");
		Frame.RenderedPose.FieldOfView = Context.EventData.GetValue<float>("FieldOfView");
		Frame.RenderedPose.ProjectionMode = static_cast<ECameraProjectionMode::Type>(Context.EventData.GetValue<uint8>("ProjectionMode"));
		Frame.RenderedPose.OrthoWidth = Context.EventData.GetValue<float>("OrthoWidth");
		Frame.RenderedPose.OrthoNearClipPlane = Context.EventData.GetValue<float>("OrthoNearClipPlane");
		Frame.RenderedPose.OrthoFarClipPlane = Context.EventData.GetValue<float>("OrthoFarClipPlane");
		Frame.RenderedPose.bConstrainAspectRatio = Context.EventData.GetValue<bool>("bConstrainAspectRatio");
		Provider.AppendActiveFrame(EventTime, MoveTemp(Frame));
		return true;
	}

	if (RouteId == RouteCCSEvaluation)
	{
		FComposableCameraEvaluationTraceFrame Frame;
		Frame.FrameCycle = Cycle;
		Frame.RecordingTime = RecordingTime;
		Frame.WorldId = Context.EventData.GetValue<uint64>("WorldId");
		Frame.SourceObjectId = Context.EventData.GetValue<uint64>("SourceObjectId");
		Frame.OwnerPawnId = Context.EventData.GetValue<uint64>("OwnerPawnId");
		Frame.PlayerControllerId = Context.EventData.GetValue<uint64>("PlayerControllerId");
		Frame.ViewTargetActorId = Context.EventData.GetValue<uint64>("ViewTargetActorId");
		Frame.SourceKind = static_cast<EComposableCameraTraceSourceKind>(Context.EventData.GetValue<uint8>("SourceKind"));
		Frame.ProjectionStatus = static_cast<EComposableCameraTraceProjectionStatus>(Context.EventData.GetValue<uint8>("ProjectionStatus"));
		Frame.CameraTypeAssetName = FName(Context.EventData.GetString("CameraTypeAssetName"));
		Frame.ContextName = FName(Context.EventData.GetString("ContextName"));
		Frame.CCSPose.Location.X = Context.EventData.GetValue<double>("LocationX");
		Frame.CCSPose.Location.Y = Context.EventData.GetValue<double>("LocationY");
		Frame.CCSPose.Location.Z = Context.EventData.GetValue<double>("LocationZ");
		Frame.CCSPose.Rotation.Pitch = Context.EventData.GetValue<double>("RotationPitch");
		Frame.CCSPose.Rotation.Yaw = Context.EventData.GetValue<double>("RotationYaw");
		Frame.CCSPose.Rotation.Roll = Context.EventData.GetValue<double>("RotationRoll");
		Frame.CCSPose.FieldOfView = Context.EventData.GetValue<float>("FieldOfView");

		const TArrayView<const uint8> Serialized = Context.EventData.GetArrayView<uint8>("SerializedPrimitives");
		TArray<uint8> Bytes;
		Bytes.Append(Serialized.GetData(), Serialized.Num());
		DeserializeComposableCameraDebugPrimitives(Bytes, Frame.Primitives);

		Provider.AppendEvaluationFrame(EventTime, MoveTemp(Frame));
		return true;
	}

	return false;
}
```

If `GetString` returns the UE 5.6 trace string view type rather than `const TCHAR*`, convert it to `FString` first. Keep the final `FName` fields.

- [ ] **Step 4: Add trace module**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceModule.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/ModuleService.h"

class FComposableCameraTraceModule : public TraceServices::IModule
{
public:
	virtual void GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& Session) override;
	virtual void GetLoggers(TArray<const TCHAR*>& OutLoggers) override;
};
```

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceModule.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraTraceModule.h"

#include "Debug/ComposableCameraTrace.h"
#include "Trace/ComposableCameraTraceAnalyzer.h"
#include "Trace/ComposableCameraTraceProvider.h"

void FComposableCameraTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = TEXT("ComposableCameraSystem");
	OutModuleInfo.DisplayName = TEXT("Composable Camera System");
}

void FComposableCameraTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& Session)
{
	FComposableCameraTraceProvider& Provider =
		Session.EditProvider<FComposableCameraTraceProvider>(
			FComposableCameraTraceProvider::ProviderName,
			Session);

	Session.AddAnalyzer(new FComposableCameraTraceAnalyzer(Session, Provider));
}

void FComposableCameraTraceModule::GetLoggers(TArray<const TCHAR*>& OutLoggers)
{
	OutLoggers.Add(*FComposableCameraTrace::LoggerName);
}
```

- [ ] **Step 5: Register editor trace module**

In `ComposableCameraSystemEditorModule.h`, add forward declarations:

```cpp
class FComposableCameraTraceModule;
class FComposableCameraRewindDebuggerExtension;
class FComposableCameraRewindDebuggerTrackCreator;
```

Add private fields:

```cpp
TUniquePtr<FComposableCameraTraceModule> TraceModule;
TSharedPtr<FComposableCameraRewindDebuggerExtension> RewindDebuggerExtension;
TSharedPtr<FComposableCameraRewindDebuggerTrackCreator> RewindDebuggerTrackCreator;
```

Add private helpers:

```cpp
void RegisterRewindDebuggerSupport();
void UnregisterRewindDebuggerSupport();
```

In `ComposableCameraSystemEditorModule.cpp`, include:

```cpp
#include "Features/IModularFeatures.h"
#include "IRewindDebugger.h"
#include "Trace/ComposableCameraTraceModule.h"
```

Add startup call after other editor registrations:

```cpp
RegisterRewindDebuggerSupport();
```

Add shutdown call before other unregister calls:

```cpp
UnregisterRewindDebuggerSupport();
```

Add helper bodies:

```cpp
void FComposableCameraSystemEditorModule::RegisterRewindDebuggerSupport()
{
	if (!TraceModule.IsValid())
	{
		TraceModule = MakeUnique<FComposableCameraTraceModule>();
		IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
	}
}

void FComposableCameraSystemEditorModule::UnregisterRewindDebuggerSupport()
{
	if (TraceModule.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
		TraceModule.Reset();
	}
}
```

Rewind extension and track creator registration are added in Task 8 after those classes exist.

- [ ] **Step 6: Run IDE compile**

Compile in Rider or Visual Studio.

Expected:

```text
Editor module compiles and registers the CCS TraceServices module.
```

- [ ] **Step 7: Commit provider/analyzer/module**

```bash
git add Source/ComposableCameraSystemEditor/Private/Trace Source/ComposableCameraSystemEditor/Public/ComposableCameraSystemEditorModule.h Source/ComposableCameraSystemEditor/Private/ComposableCameraSystemEditorModule.cpp
git commit -m "feat: ingest CCS rewind trace events"
```

---

### Task 8: Rewind Extension, Matching, and 3D Playback Drawing

**Files:**

- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.cpp`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerTrack.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerTrack.cpp`
- Modify: `Source/ComposableCameraSystemEditor/Public/ComposableCameraSystemEditorModule.h`
- Modify: `Source/ComposableCameraSystemEditor/Private/ComposableCameraSystemEditorModule.cpp`

- [ ] **Step 1: Add rewind extension header**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"
#include "IRewindDebuggerExtension.h"

class FComposableCameraRewindDebuggerExtension : public IRewindDebuggerExtension
{
public:
	virtual ~FComposableCameraRewindDebuggerExtension() override;

	virtual void RecordingStarted(IRewindDebugger* RewindDebugger) override;
	virtual void RecordingStopped(IRewindDebugger* RewindDebugger) override;
	virtual void Update(float DeltaTime, IRewindDebugger* RewindDebugger) override;
	virtual void Clear(IRewindDebugger* RewindDebugger) override;

private:
	void EnsureDebugDrawDelegate(bool bRegistered);
	void DebugDraw(UCanvas* Canvas, APlayerController* PlayerController);
	void DrawPrimitive(UWorld* World, const FComposableCameraDebugPrimitive& Primitive) const;
	bool FindPlaybackFrames(
		IRewindDebugger* RewindDebugger,
		FComposableCameraActiveTraceFrame& OutActiveFrame,
		FComposableCameraEvaluationTraceFrame& OutEvaluationFrame,
		bool& bOutHasEvaluationFrame) const;

private:
	FDelegateHandle DebugDrawDelegateHandle;
	TWeakObjectPtr<UWorld> VisualizedWorld;
	FComposableCameraActiveTraceFrame ActiveFrame;
	FComposableCameraEvaluationTraceFrame EvaluationFrame;
	bool bHasActiveFrame = false;
	bool bHasEvaluationFrame = false;
	double LastTraceTime = -1.0;
};
```

- [ ] **Step 2: Add rewind extension implementation**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraRewindDebuggerExtension.h"

#include "Debug/DebugDrawService.h"
#include "Debug/ComposableCameraTrace.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Trace/ComposableCameraTraceProvider.h"
#include "TraceServices/Model/Frames.h"

FComposableCameraRewindDebuggerExtension::~FComposableCameraRewindDebuggerExtension()
{
	EnsureDebugDrawDelegate(false);
}

void FComposableCameraRewindDebuggerExtension::RecordingStarted(IRewindDebugger* RewindDebugger)
{
	UE::Trace::ToggleChannel(*FComposableCameraTrace::ChannelName, true);
}

void FComposableCameraRewindDebuggerExtension::RecordingStopped(IRewindDebugger* RewindDebugger)
{
	UE::Trace::ToggleChannel(*FComposableCameraTrace::ChannelName, false);
}

void FComposableCameraRewindDebuggerExtension::Clear(IRewindDebugger* RewindDebugger)
{
	EnsureDebugDrawDelegate(false);
	VisualizedWorld.Reset();
	bHasActiveFrame = false;
	bHasEvaluationFrame = false;
	LastTraceTime = -1.0;
}

void FComposableCameraRewindDebuggerExtension::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
	if (!RewindDebugger || RewindDebugger->IsPIESimulating() || RewindDebugger->GetRecordingDuration() == 0.0)
	{
		EnsureDebugDrawDelegate(false);
		return;
	}

	EnsureDebugDrawDelegate(true);

	const double CurrentTraceTime = RewindDebugger->CurrentTraceTime();
	if (CurrentTraceTime == LastTraceTime)
	{
		return;
	}

	LastTraceTime = CurrentTraceTime;
	VisualizedWorld = RewindDebugger->GetWorldToVisualize();

	FComposableCameraActiveTraceFrame NewActiveFrame;
	FComposableCameraEvaluationTraceFrame NewEvaluationFrame;
	bool bNewHasEvaluationFrame = false;

	bHasActiveFrame = FindPlaybackFrames(
		RewindDebugger,
		NewActiveFrame,
		NewEvaluationFrame,
		bNewHasEvaluationFrame);

	if (bHasActiveFrame)
	{
		ActiveFrame = MoveTemp(NewActiveFrame);
		EvaluationFrame = MoveTemp(NewEvaluationFrame);
		bHasEvaluationFrame = bNewHasEvaluationFrame;
	}
	else
	{
		bHasEvaluationFrame = false;
	}
}

void FComposableCameraRewindDebuggerExtension::EnsureDebugDrawDelegate(bool bRegistered)
{
	if (bRegistered && !DebugDrawDelegateHandle.IsValid())
	{
		DebugDrawDelegateHandle = UDebugDrawService::Register(
			TEXT("GameplayDebug"),
			FDebugDrawDelegate::CreateRaw(this, &FComposableCameraRewindDebuggerExtension::DebugDraw));
	}
	else if (!bRegistered && DebugDrawDelegateHandle.IsValid())
	{
		UDebugDrawService::Unregister(DebugDrawDelegateHandle);
		DebugDrawDelegateHandle.Reset();
	}
}

bool FComposableCameraRewindDebuggerExtension::FindPlaybackFrames(
	IRewindDebugger* RewindDebugger,
	FComposableCameraActiveTraceFrame& OutActiveFrame,
	FComposableCameraEvaluationTraceFrame& OutEvaluationFrame,
	bool& bOutHasEvaluationFrame) const
{
	bOutHasEvaluationFrame = false;

	const TraceServices::IAnalysisSession* AnalysisSession = RewindDebugger->GetAnalysisSession();
	if (!AnalysisSession)
	{
		return false;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

	const FComposableCameraTraceProvider* Provider =
		AnalysisSession->ReadProvider<FComposableCameraTraceProvider>(
			FComposableCameraTraceProvider::ProviderName);
	if (!Provider || !Provider->GetActiveTimeline())
	{
		return false;
	}

	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*AnalysisSession);
	TraceServices::FFrame Frame;
	if (!FrameProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, RewindDebugger->CurrentTraceTime(), Frame))
	{
		return false;
	}

	bool bFoundActive = false;
	Provider->GetActiveTimeline()->EnumerateEvents(
		Frame.StartTime,
		Frame.EndTime,
		[&](double EventStartTime, double EventEndTime, uint32 Depth, const FComposableCameraActiveTraceFrame& FrameData)
		{
			OutActiveFrame = FrameData;
			bFoundActive = true;
			return TraceServices::EEventEnumerate::Continue;
		});

	if (!bFoundActive || !Provider->GetEvaluationTimeline())
	{
		return bFoundActive;
	}

	Provider->GetEvaluationTimeline()->EnumerateEvents(
		Frame.StartTime,
		Frame.EndTime,
		[&](double EventStartTime, double EventEndTime, uint32 Depth, const FComposableCameraEvaluationTraceFrame& FrameData)
		{
			if (DoesComposableCameraEvaluationMatchActiveFrame(OutActiveFrame, FrameData))
			{
				OutEvaluationFrame = FrameData;
				bOutHasEvaluationFrame = true;
			}
			return TraceServices::EEventEnumerate::Continue;
		});

	return true;
}

void FComposableCameraRewindDebuggerExtension::DebugDraw(UCanvas* Canvas, APlayerController* PlayerController)
{
	UWorld* World = VisualizedWorld.Get();
	if (!World || !bHasActiveFrame)
	{
		return;
	}

	FComposableCameraDebugPrimitive Frustum =
		FComposableCameraDebugPrimitive::MakeCameraFrustum(
			ActiveFrame.RenderedPose,
			FColor(80, 200, 255),
			SDPG_Foreground);
	DrawPrimitive(World, Frustum);

	if (bHasEvaluationFrame)
	{
		for (const FComposableCameraDebugPrimitive& Primitive : EvaluationFrame.Primitives)
		{
			DrawPrimitive(World, Primitive);
		}
	}
}

void FComposableCameraRewindDebuggerExtension::DrawPrimitive(
	UWorld* World,
	const FComposableCameraDebugPrimitive& Primitive) const
{
	switch (Primitive.Kind)
	{
	case EComposableCameraDebugPrimitiveKind::Line:
		DrawDebugLine(World, Primitive.A, Primitive.B, Primitive.Color, false, -1.0f, Primitive.DepthPriority, Primitive.Thickness);
		break;
	case EComposableCameraDebugPrimitiveKind::Point:
		DrawDebugPoint(World, Primitive.A, Primitive.Size, Primitive.Color, false, -1.0f, Primitive.DepthPriority);
		break;
	case EComposableCameraDebugPrimitiveKind::Sphere:
	case EComposableCameraDebugPrimitiveKind::SolidSphere:
		DrawDebugSphere(World, Primitive.A, Primitive.Radius, 12, FColor(Primitive.Color.R, Primitive.Color.G, Primitive.Color.B, Primitive.Alpha), false, -1.0f, Primitive.DepthPriority);
		break;
	case EComposableCameraDebugPrimitiveKind::Box:
		DrawDebugBox(World, Primitive.A, Primitive.Extent, Primitive.Rotation.Quaternion(), Primitive.Color, false, -1.0f, Primitive.DepthPriority);
		break;
	case EComposableCameraDebugPrimitiveKind::CameraFrustum:
		DrawDebugCamera(World, Primitive.A, Primitive.Rotation, Primitive.Radius, 100.0f, Primitive.Color, false, -1.0f, Primitive.DepthPriority);
		break;
	}
}
```

- [ ] **Step 3: Add track creator**

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerTrack.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRewindDebuggerTrackCreator.h"

class FComposableCameraRewindDebuggerTrackCreator : public RewindDebugger::IRewindDebuggerTrackCreator
{
public:
	virtual FName GetTargetTypeNameInternal() const override;
	virtual FName GetNameInternal() const override;
	virtual void GetTrackTypesInternal(TArray<FRewindDebuggerTrackType>& Types) const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> CreateTrackInternal(uint64 ObjectId) const override;
};
```

Create `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerTrack.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraRewindDebuggerTrack.h"

#include "GameFramework/Pawn.h"
#include "IRewindDebuggerTrack.h"

namespace
{
	class FComposableCameraRewindDebuggerTrack : public RewindDebugger::FRewindDebuggerTrack
	{
	public:
		explicit FComposableCameraRewindDebuggerTrack(uint64 InObjectId)
			: ObjectId(InObjectId)
		{
		}

		virtual uint64 GetObjectId() const override
		{
			return ObjectId;
		}

		virtual FText GetDisplayNameInternal() const override
		{
			return NSLOCTEXT("ComposableCameraRewindDebugger", "TrackName", "Composable Camera");
		}

	private:
		uint64 ObjectId = 0;
	};
}

FName FComposableCameraRewindDebuggerTrackCreator::GetTargetTypeNameInternal() const
{
	return APawn::StaticClass()->GetFName();
}

FName FComposableCameraRewindDebuggerTrackCreator::GetNameInternal() const
{
	return TEXT("ComposableCameraSystem");
}

void FComposableCameraRewindDebuggerTrackCreator::GetTrackTypesInternal(
	TArray<FRewindDebuggerTrackType>& Types) const
{
	Types.Add({ TEXT("ComposableCameraSystem"), NSLOCTEXT("ComposableCameraRewindDebugger", "TrackType", "Composable Camera") });
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FComposableCameraRewindDebuggerTrackCreator::CreateTrackInternal(
	uint64 ObjectId) const
{
	return MakeShared<FComposableCameraRewindDebuggerTrack>(ObjectId);
}
```

These virtual names match UE 5.6 `IRewindDebuggerTrackCreator`. Keep target type `APawn`.

- [ ] **Step 4: Register extension and track creator**

In `ComposableCameraSystemEditorModule.cpp`, include:

```cpp
#include "Trace/ComposableCameraRewindDebuggerExtension.h"
#include "Trace/ComposableCameraRewindDebuggerTrack.h"
```

Extend `RegisterRewindDebuggerSupport`:

```cpp
if (!RewindDebuggerExtension.IsValid())
{
	RewindDebuggerExtension = MakeShared<FComposableCameraRewindDebuggerExtension>();
	IModularFeatures::Get().RegisterModularFeature(
		IRewindDebuggerExtension::ModularFeatureName,
		RewindDebuggerExtension.Get());
}

if (!RewindDebuggerTrackCreator.IsValid())
{
	RewindDebuggerTrackCreator = MakeShared<FComposableCameraRewindDebuggerTrackCreator>();
	IModularFeatures::Get().RegisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName,
		RewindDebuggerTrackCreator.Get());
}
```

Extend `UnregisterRewindDebuggerSupport` in reverse order:

```cpp
if (RewindDebuggerTrackCreator.IsValid())
{
	IModularFeatures::Get().UnregisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName,
		RewindDebuggerTrackCreator.Get());
	RewindDebuggerTrackCreator.Reset();
}

if (RewindDebuggerExtension.IsValid())
{
	IModularFeatures::Get().UnregisterModularFeature(
		IRewindDebuggerExtension::ModularFeatureName,
		RewindDebuggerExtension.Get());
	RewindDebuggerExtension.Reset();
}
```

- [ ] **Step 5: Run IDE compile**

Compile in Rider or Visual Studio.

Expected:

```text
Editor module compiles.
Composable Camera track appears under Pawn selections in Rewind Debugger.
Recording toggles ComposableCameraSystemChannel.
```

- [ ] **Step 6: Manual Rewind smoke check**

Run in Unreal Editor:

```text
1. Start PIE.
2. Open Rewind Debugger.
3. Select the controlled pawn.
4. Start recording.
5. Move with a CCS gameplay camera.
6. Stop recording.
7. Scrub recorded frames.
```

Expected:

```text
Historical camera frustum draws in 3D space.
CCS gizmos draw when a CCS evaluation frame matches the active camera frame.
Editor viewport camera is not forced into the recorded camera view.
```

- [ ] **Step 7: Commit Rewind playback**

```bash
git add Source/ComposableCameraSystemEditor/Private/Trace Source/ComposableCameraSystemEditor/Public/ComposableCameraSystemEditorModule.h Source/ComposableCameraSystemEditor/Private/ComposableCameraSystemEditorModule.cpp
git commit -m "feat: draw CCS rewind debugger playback"
```

---

### Task 9: Documentation, Review, and Final Handoff

**Files:**

- Modify: `Docs/DesignDoc.md`
- Modify: `Docs/TechDoc.md`
- Modify: `Docs/ExecutionFlowExamples.md`
- Read: `.agents/skills/composable-camera-review/SKILL.md`

- [ ] **Step 1: Update DesignDoc**

In `Docs/DesignDoc.md`, update `Updated:` to:

```markdown
Updated: 2026-06-17
```

In `## 14. Debugging`, add:

```markdown
### Rewind Debugger trace playback

CCS Rewind Debugger support uses two trace streams.

`ActiveCamera` frames are written by the player camera manager after the final
camera cache is filled. They are the authority for the historical rendered
camera pose shown during Rewind playback.

`CCSEvaluation` frames are written by CCS evaluators. Gameplay cameras write
them from `AComposableCameraPlayerCameraManager` after context-stack
evaluation. Level Sequence cameras write them from
`UComposableCameraLevelSequenceComponent` after internal camera tick, Sequencer
patch overlays, and CineCamera projection status resolution.

Rewind playback selects the character/pawn track, reads the active camera
frame for the current trace time, draws its historical frustum, then matches a
CCS evaluation frame from the same frame. Gameplay CCS matches by PCM id and
frame cycle. Level Sequence CCS matches by active view target actor id. If no
CCS evaluation frame matches, playback draws the historical frustum only.

Playback never evaluates live cameras, directors, nodes, transitions, or Level
Sequence components. It draws only serialized trace data.
```

- [ ] **Step 2: Update TechDoc**

In `Docs/TechDoc.md`, update `Updated:` to:

```markdown
Updated: 2026-06-17
```

Add a technique section under runtime debug techniques:

```markdown
### Rewind trace primitives

3D node and transition gizmos route through `FComposableCameraDebugDrawSink`.
The live sink adapts to `DrawDebug*` and the capture sink appends
`FComposableCameraDebugPrimitive` values. This keeps live viewport gizmos and
Rewind playback gizmos on the same code path.

Trace capture is gated by `FComposableCameraTrace::IsTraceEnabled()`, which
checks `CCS.Debug.Trace` and the UE trace channel. When disabled, the hot path
does not build primitive arrays or serialize archives.

Do not add direct `DrawDebug*` calls inside `DrawNodeDebug` or
`DrawTransitionDebug`. Use the sink. Direct draw calls cannot be replayed by
Rewind Debugger.
```

- [ ] **Step 3: Update ExecutionFlowExamples**

In `Docs/ExecutionFlowExamples.md`, add:

```markdown
## Rewind Debugger camera playback

Gameplay CCS:

```text
PCM DoUpdateCamera
  -> ContextStack->Evaluate
  -> capture CCS node/transition primitives through trace sink
  -> write CCSEvaluation(source = CCS_PCM)
  -> FillCameraCache
  -> write ActiveCamera(source = CCS_PCM)
  -> Rewind playback draws ActiveCamera frustum + matched CCS primitives
```

Level Sequence CCS:

```text
LS Component EvaluateOnce
  -> InternalCamera->TickCamera
  -> Sequencer patch overlays
  -> ProjectPoseToCineCamera
  -> write CCSEvaluation(source = CCS_LevelSequence)
PCM DoUpdateCamera
  -> UE view-target path reads LS actor CineCamera
  -> write ActiveCamera(source = CCS_LevelSequence)
Rewind playback
  -> selected pawn track reads ActiveCamera
  -> view target actor id matches LS CCSEvaluation
  -> draw ActiveCamera frustum + LS CCS primitives
```
```

- [ ] **Step 4: Run project review checklist**

Read `.agents/skills/composable-camera-review/SKILL.md` and run the checklist.

Specific checks:

```text
No direct DrawDebug* calls remain in node/transition debug overrides.
Trace capture is skipped when FComposableCameraTrace::IsTraceEnabled() is false.
Playback does not dereference live CCS camera/node/director/LS evaluator objects.
Runtime trace code is stripped from shipping/test builds.
Docs describe both PCM and LS flows.
```

- [ ] **Step 5: Full IDE verification request**

Ask the user to run a full build in Rider or Visual Studio. Header signatures changed, so Live Coding is not enough.

Expected handoff:

```text
Full IDE build + editor restart required. Live Coding not enough because public headers and virtual signatures changed.
```

- [ ] **Step 6: Manual Rewind verification request**

Ask the user to run:

```text
1. Start PIE.
2. Select controlled pawn in Rewind Debugger.
3. Record gameplay with normal CCS gameplay camera.
4. Stop and scrub. Confirm historical frustum + node gizmos.
5. Record gameplay that triggers LS camera.
6. Stop and scrub through LS range. Confirm same pawn track shows LS frustum + LS node gizmos.
7. Record native non-CCS camera. Confirm frustum appears without CCS gizmos.
8. Confirm editor viewport is not forced to look through the recorded camera.
```

- [ ] **Step 7: Commit docs and final fixes**

Docs are ignored by `.gitignore`, so force-add docs:

```bash
git add -f Docs/DesignDoc.md Docs/TechDoc.md Docs/ExecutionFlowExamples.md
git add Source/ComposableCameraSystem Source/ComposableCameraSystemEditor
git commit -m "docs: document CCS rewind debugger support"
```

If compile feedback required source fixes, commit those fixes before or with the docs when they are part of the same Rewind Debugger feature.
