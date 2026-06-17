// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraTraceProvider.h"

const FName FComposableCameraTraceProvider::ProviderName(TEXT("ComposableCameraSystemTraceProvider"));

FComposableCameraTraceProvider::FComposableCameraTraceProvider(
	TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
	, ActiveTimeline(MakeShared<TraceServices::TPointTimeline<FComposableCameraActiveTraceFrame>>(Session.GetLinearAllocator()))
	, EvaluationTimeline(MakeShared<TraceServices::TPointTimeline<FComposableCameraEvaluationTraceFrame>>(Session.GetLinearAllocator()))
{
}

void FComposableCameraTraceProvider::AppendActiveFrame(
	double EventTime,
	FComposableCameraActiveTraceFrame&& Frame)
{
	Session.WriteAccessCheck();
	ActiveTimeline->EmplaceEvent(EventTime, MoveTemp(Frame));
	Session.UpdateDurationSeconds(EventTime);
}

void FComposableCameraTraceProvider::AppendEvaluationFrame(
	double EventTime,
	FComposableCameraEvaluationTraceFrame&& Frame)
{
	Session.WriteAccessCheck();
	EvaluationTimeline->EmplaceEvent(EventTime, MoveTemp(Frame));
	Session.UpdateDurationSeconds(EventTime);
}

const FComposableCameraTraceProvider::FActiveCameraTimeline* FComposableCameraTraceProvider::GetActiveTimeline() const
{
	Session.ReadAccessCheck();
	return &ActiveTimeline.Get();
}

const FComposableCameraTraceProvider::FEvaluationTimeline* FComposableCameraTraceProvider::GetEvaluationTimeline() const
{
	Session.ReadAccessCheck();
	return &EvaluationTimeline.Get();
}
