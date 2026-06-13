// Copyright 2026 Sulley. All Rights Reserved.

#include "ComposableCameraSystemModule.h"

#include "Debug/ComposableCameraDebugPanel.h"
#include "Debug/ComposableCameraLogCapture.h"
#include "Debug/ComposableCameraShotZoneOverlay.h"
#include "Debug/ComposableCameraViewportDebug.h"

#define LOCTEXT_NAMESPACE "FComposableCameraSystemModule"

DEFINE_LOG_CATEGORY(LogComposableCameraSystem)

void FComposableCameraSystemModule::StartupModule()
{
	// Install log capture BEFORE the two draw hooks so any warnings /
	// errors emitted during their registration are captured too.
	// No-op in shipping (Install is `#if !UE_BUILD_SHIPPING`).
#if !UE_BUILD_SHIPPING
	FComposableCameraLogCapture::Install();
#endif

	// Register two independent UDebugDrawService hooks:
	//   - FComposableCameraDebugPanel      (2D HUD, `CCS.Debug.Panel` CVar)
	//   - FComposableCameraViewportDebug   (3D world gizmos, `CCS.Debug.Viewport` CVar)
	// Each draw delegate early-outs on its own CVar, so the "disabled"
	// cost is one CVar read per viewport per frame.
	FComposableCameraDebugPanel::Initialize();
	FComposableCameraViewportDebug::Initialize();
	FComposableCameraShotZoneOverlay::Initialize();
}

void FComposableCameraSystemModule::ShutdownModule()
{
	FComposableCameraShotZoneOverlay::Shutdown();
	FComposableCameraViewportDebug::Shutdown();
	FComposableCameraDebugPanel::Shutdown();

#if !UE_BUILD_SHIPPING
	// Uninstall last so we still catch warnings from the shutdown paths
	// of the draw hooks. Removing from GLog prevents dangling-pointer
	// Serialize() calls if any CCS logs fire after DLL unload.
	FComposableCameraLogCapture::Uninstall();
#endif
}

IMPLEMENT_MODULE(FComposableCameraSystemModule, ComposableCameraSystem)

#undef LOCTEXT_NAMESPACE
