// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Trace/Analyzer.h"

namespace TraceServices
{
class IAnalysisSession;
}

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
		RouteActiveCamera,
		RouteCCSEvaluation,
	};

	TraceServices::IAnalysisSession& Session;
	FComposableCameraTraceProvider& Provider;
};
