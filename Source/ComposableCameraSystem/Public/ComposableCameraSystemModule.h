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

// COMPOSABLECAMERASYSTEM_API on the declaration is required so other modules
// (notably ComposableCameraSystemUncookedOnly's UK2Node_ActivateComposableCamera)
// can resolve the LogComposableCameraSystem symbol at link time. Without the
// export macro, DECLARE_LOG_CATEGORY_EXTERN declares an unqualified extern that
// only the runtime module's own DLL can resolve, producing LNK2001 in any
// dependent module that calls UE_LOG(LogComposableCameraSystem, ...).
COMPOSABLECAMERASYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogComposableCameraSystem, Log, All);