// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"
#include "Model/PointTimeline.h"
#include "TraceServices/Model/AnalysisSession.h"

class FComposableCameraTraceProvider : public TraceServices::IProvider
{
public:
	static const FName ProviderName;

	using FActiveCameraTimeline = TraceServices::ITimeline<FComposableCameraActiveTraceFrame>;
	using FEvaluationTimeline = TraceServices::ITimeline<FComposableCameraEvaluationTraceFrame>;

	explicit FComposableCameraTraceProvider(TraceServices::IAnalysisSession& InSession);

	void AppendActiveFrame(double EventTime, FComposableCameraActiveTraceFrame&& Frame);
	void AppendEvaluationFrame(double EventTime, FComposableCameraEvaluationTraceFrame&& Frame);

	const FActiveCameraTimeline* GetActiveTimeline() const;
	const FEvaluationTimeline* GetEvaluationTimeline() const;

private:
	TraceServices::IAnalysisSession& Session;
	TSharedRef<TraceServices::TPointTimeline<FComposableCameraActiveTraceFrame>> ActiveTimeline;
	TSharedRef<TraceServices::TPointTimeline<FComposableCameraEvaluationTraceFrame>> EvaluationTimeline;
};
