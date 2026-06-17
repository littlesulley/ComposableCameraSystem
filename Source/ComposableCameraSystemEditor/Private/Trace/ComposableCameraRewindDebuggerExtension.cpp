// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraRewindDebuggerExtension.h"

#include "Debug/ComposableCameraViewportDebug.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "IRewindDebugger.h"
#include "Trace/ComposableCameraTraceProvider.h"
#include "Trace/Trace.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"

namespace
{
	const TCHAR* GComposableCameraTraceChannelName = TEXT("ComposableCameraSystemChannel");

	static int32 GetComposableCameraPrimitiveSegments(const FComposableCameraDebugPrimitive& Primitive)
	{
		const int32 Segments = Primitive.Size > 0.0f ? FMath::RoundToInt(Primitive.Size) : 12;
		return FMath::Clamp(Segments, 4, 32);
	}

	static float GetComposableCameraFrustumScale(const FComposableCameraDebugPrimitive& Primitive)
	{
		return Primitive.Thickness > 0.0f ? Primitive.Thickness : 1.0f;
	}
}

FComposableCameraRewindDebuggerExtension::~FComposableCameraRewindDebuggerExtension()
{
	EnsureDebugDrawDelegate(false);
}

void FComposableCameraRewindDebuggerExtension::RecordingStarted(IRewindDebugger* RewindDebugger)
{
	UE::Trace::ToggleChannel(GComposableCameraTraceChannelName, true);
}

void FComposableCameraRewindDebuggerExtension::RecordingStopped(IRewindDebugger* RewindDebugger)
{
	UE::Trace::ToggleChannel(GComposableCameraTraceChannelName, false);
}

void FComposableCameraRewindDebuggerExtension::Clear(IRewindDebugger* RewindDebugger)
{
	EnsureDebugDrawDelegate(false);
	VisualizedWorld.Reset();
	bHasActiveFrame = false;
	bHasEvaluationFrame = false;
	LastTraceTime = -1.0;
	LastTargetActorId = 0;
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
	const uint64 TargetActorId = RewindDebugger->GetTargetActorId();
	if (CurrentTraceTime == LastTraceTime && TargetActorId == LastTargetActorId && bHasActiveFrame)
	{
		return;
	}

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
		LastTraceTime = CurrentTraceTime;
		LastTargetActorId = TargetActorId;
		ActiveFrame = MoveTemp(NewActiveFrame);
		EvaluationFrame = MoveTemp(NewEvaluationFrame);
		bHasEvaluationFrame = bNewHasEvaluationFrame;
	}
	else
	{
		LastTraceTime = -1.0;
		LastTargetActorId = 0;
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

	const TraceServices::IAnalysisSession* AnalysisSession = RewindDebugger ? RewindDebugger->GetAnalysisSession() : nullptr;
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

	const uint64 TargetActorId = RewindDebugger->GetTargetActorId();
	bool bFoundActive = false;
	Provider->GetActiveTimeline()->EnumerateEvents(
		Frame.StartTime,
		Frame.EndTime,
		[this, TargetActorId, &OutActiveFrame, &bFoundActive](
			double EventStartTime,
			double EventEndTime,
			uint32 Depth,
			const FComposableCameraActiveTraceFrame& FrameData)
		{
			if (IsActiveFrameForTarget(FrameData, TargetActorId))
			{
				OutActiveFrame = FrameData;
				bFoundActive = true;
			}
			return TraceServices::EEventEnumerate::Continue;
		});

	if (!bFoundActive || !Provider->GetEvaluationTimeline())
	{
		return bFoundActive;
	}

	Provider->GetEvaluationTimeline()->EnumerateEvents(
		Frame.StartTime,
		Frame.EndTime,
		[this, &OutActiveFrame, &OutEvaluationFrame, &bOutHasEvaluationFrame](
			double EventStartTime,
			double EventEndTime,
			uint32 Depth,
			const FComposableCameraEvaluationTraceFrame& FrameData)
		{
			if (DoesEvaluationMatchActiveFrameForPlayback(OutActiveFrame, FrameData))
			{
				OutEvaluationFrame = FrameData;
				bOutHasEvaluationFrame = true;
			}
			return TraceServices::EEventEnumerate::Continue;
		});

	return true;
}

bool FComposableCameraRewindDebuggerExtension::IsActiveFrameForTarget(
	const FComposableCameraActiveTraceFrame& Frame,
	uint64 TargetActorId) const
{
	return TargetActorId == 0
		|| Frame.PawnId == TargetActorId
		|| Frame.ViewTargetActorId == TargetActorId;
}

bool FComposableCameraRewindDebuggerExtension::DoesEvaluationMatchActiveFrameForPlayback(
	const FComposableCameraActiveTraceFrame& Active,
	const FComposableCameraEvaluationTraceFrame& Evaluation) const
{
	if (DoesComposableCameraEvaluationMatchActiveFrame(Active, Evaluation))
	{
		return true;
	}

	if (Evaluation.SourceKind == EComposableCameraTraceSourceKind::CCS_LevelSequence)
	{
		return Active.ViewTargetActorId != 0
			&& Active.ViewTargetActorId == Evaluation.ViewTargetActorId;
	}

	return false;
}

void FComposableCameraRewindDebuggerExtension::DebugDraw(UCanvas* Canvas, APlayerController* PlayerController)
{
	UWorld* World = VisualizedWorld.Get();
	if (!World || !bHasActiveFrame)
	{
		return;
	}

	FComposableCameraDebugPrimitive ActiveFrustum =
		FComposableCameraDebugPrimitive::MakeCameraFrustum(
			ActiveFrame.RenderedPose,
			FColor(80, 200, 255),
			SDPG_Foreground,
			100.0f);
	DrawPrimitive(World, ActiveFrustum);

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
	if (!World)
	{
		return;
	}

	const FColor ColorWithAlpha(Primitive.Color.R, Primitive.Color.G, Primitive.Color.B, Primitive.Alpha);

	switch (Primitive.Kind)
	{
	case EComposableCameraDebugPrimitiveKind::Line:
		DrawDebugLine(World, Primitive.A, Primitive.B, Primitive.Color, false, -1.0f, Primitive.DepthPriority, Primitive.Thickness);
		break;
	case EComposableCameraDebugPrimitiveKind::Point:
		DrawDebugPoint(World, Primitive.A, Primitive.Size, Primitive.Color, false, -1.0f, Primitive.DepthPriority);
		break;
	case EComposableCameraDebugPrimitiveKind::Sphere:
		DrawDebugSphere(
			World,
			Primitive.A,
			Primitive.Radius,
			GetComposableCameraPrimitiveSegments(Primitive),
			ColorWithAlpha,
			false,
			-1.0f,
			Primitive.DepthPriority,
			Primitive.Thickness);
		break;
	case EComposableCameraDebugPrimitiveKind::SolidSphere:
		FComposableCameraViewportDebug::DrawSolidDebugSphere(
			World,
			Primitive.A,
			Primitive.Radius,
			Primitive.Color,
			Primitive.Alpha,
			GetComposableCameraPrimitiveSegments(Primitive),
			Primitive.DepthPriority);
		break;
	case EComposableCameraDebugPrimitiveKind::Box:
		DrawDebugBox(
			World,
			Primitive.A,
			Primitive.Extent,
			Primitive.Rotation.Quaternion(),
			Primitive.Color,
			false,
			-1.0f,
			Primitive.DepthPriority,
			Primitive.Thickness);
		break;
	case EComposableCameraDebugPrimitiveKind::CameraFrustum:
		DrawDebugCamera(
			World,
			Primitive.A,
			Primitive.Rotation,
			Primitive.Radius,
			GetComposableCameraFrustumScale(Primitive),
			Primitive.Color,
			false,
			-1.0f,
			Primitive.DepthPriority);
		break;
	case EComposableCameraDebugPrimitiveKind::Plane:
		if (!Primitive.B.IsNearlyZero())
		{
			const FPlane Plane(Primitive.A, Primitive.B.GetSafeNormal());
			DrawDebugSolidPlane(
				World,
				Plane,
				Primitive.A,
				FVector2D(Primitive.Extent.X, Primitive.Extent.Y),
				Primitive.Color,
				false,
				-1.0f,
				Primitive.DepthPriority);
		}
		break;
	default:
		break;
	}
}
