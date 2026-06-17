// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"
#include "IRewindDebuggerExtension.h"

class APlayerController;
class UCanvas;
class UWorld;

class FComposableCameraRewindDebuggerExtension : public IRewindDebuggerExtension
{
public:
	virtual ~FComposableCameraRewindDebuggerExtension();

	virtual FString GetName() override { return TEXT("ComposableCameraSystem"); }
	virtual void RecordingStarted(IRewindDebugger* RewindDebugger) override;
	virtual void RecordingStopped(IRewindDebugger* RewindDebugger) override;
	virtual void Update(float DeltaTime, IRewindDebugger* RewindDebugger) override;
	virtual void Clear(IRewindDebugger* RewindDebugger) override;

private:
	void EnsureDebugDrawDelegate(bool bRegistered);
	void DebugDraw(UCanvas* Canvas, APlayerController* PlayerController);
	void DrawPrimitive(UWorld* World, const FComposableCameraDebugPrimitive& Primitive) const;
	bool GetTargetActorIdForPlayback(IRewindDebugger* RewindDebugger, uint64& OutTargetActorId) const;
	bool FindPlaybackFrames(
		IRewindDebugger* RewindDebugger,
		uint64 TargetActorId,
		FComposableCameraActiveTraceFrame& OutActiveFrame,
		FComposableCameraEvaluationTraceFrame& OutEvaluationFrame,
		bool& bOutHasEvaluationFrame) const;
	bool IsActiveFrameForTarget(
		const FComposableCameraActiveTraceFrame& Frame,
		uint64 TargetActorId) const;
	bool DoesEvaluationMatchActiveFrameForPlayback(
		const FComposableCameraActiveTraceFrame& Active,
		const FComposableCameraEvaluationTraceFrame& Evaluation) const;

private:
	FDelegateHandle DebugDrawDelegateHandle;
	TWeakObjectPtr<UWorld> VisualizedWorld;
	FComposableCameraActiveTraceFrame ActiveFrame;
	FComposableCameraEvaluationTraceFrame EvaluationFrame;
	bool bHasActiveFrame = false;
	bool bHasEvaluationFrame = false;
	double LastTraceTime = -1.0;
	uint64 LastTargetActorId = 0;
};
