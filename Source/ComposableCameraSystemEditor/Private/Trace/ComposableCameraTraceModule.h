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
	virtual const TCHAR* GetCommandLineArgument() override { return TEXT("ccsrewindtrace"); }
};
