// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraTraceTypes.h"

#if WITH_EDITOR
#include "Trace/Config.h"
#define UE_COMPOSABLE_CAMERA_TRACE (UE_TRACE_ENABLED && !IS_PROGRAM && !UE_BUILD_SHIPPING && !UE_BUILD_TEST)
#else
#define UE_COMPOSABLE_CAMERA_TRACE 0
#endif

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
