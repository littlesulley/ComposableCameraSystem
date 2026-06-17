// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraTraceModule.h"

#include "Trace/ComposableCameraTraceAnalyzer.h"
#include "Trace/ComposableCameraTraceProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

void FComposableCameraTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
{
	static const FName ModuleName(TEXT("ComposableCameraSystemTrace"));
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("Composable Camera System");
}

void FComposableCameraTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& Session)
{
	TSharedPtr<FComposableCameraTraceProvider> Provider = MakeShared<FComposableCameraTraceProvider>(Session);
	Session.AddProvider(FComposableCameraTraceProvider::ProviderName, Provider);
	Session.AddAnalyzer(new FComposableCameraTraceAnalyzer(Session, *Provider));
}

void FComposableCameraTraceModule::GetLoggers(TArray<const TCHAR*>& OutLoggers)
{
	OutLoggers.Add(TEXT("ComposableCameraSystem"));
}
