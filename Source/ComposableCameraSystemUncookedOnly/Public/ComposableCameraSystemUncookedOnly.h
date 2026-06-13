// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

class FComposableCameraGraphPanelPinFactory;

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FComposableCameraSystemUncookedOnlyModule: public IModuleInterface
{
public:
 virtual void StartupModule() override;
 virtual void ShutdownModule() override;

private:
 TSharedPtr<FComposableCameraGraphPanelPinFactory> ComposableCameraGraphPanelPinFactory;
};
