// Copyright Sulley. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FComposableCameraSystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

DECLARE_LOG_CATEGORY_EXTERN(LogComposableCameraSystem, Log, All);