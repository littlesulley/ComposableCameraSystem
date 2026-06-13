// Copyright 2026 Sulley. All Rights Reserved.

#include "ComposableCameraSystemUncookedOnly.h"

#include "ComposableCameraGraphPanelPinFactory.h"

#define LOCTEXT_NAMESPACE "FComposableCameraSystemUncookedOnlyModule"

void FComposableCameraSystemUncookedOnlyModule::StartupModule()
{
 ComposableCameraGraphPanelPinFactory = MakeShareable(new FComposableCameraGraphPanelPinFactory());
 FEdGraphUtilities::RegisterVisualPinFactory(ComposableCameraGraphPanelPinFactory);
}

void FComposableCameraSystemUncookedOnlyModule::ShutdownModule()
{
 FEdGraphUtilities::UnregisterVisualPinFactory(ComposableCameraGraphPanelPinFactory);
}

#undef LOCTEXT_NAMESPACE
 
IMPLEMENT_MODULE(FComposableCameraSystemUncookedOnlyModule, ComposableCameraSystemUncookedOnly)