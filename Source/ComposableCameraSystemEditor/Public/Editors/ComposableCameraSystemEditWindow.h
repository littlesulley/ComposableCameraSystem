// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSpawnTabArgs;
class SDockTab;
class AComposableCameraCameraBase;

/** Global dockable tools window. Registered for the editor module's lifetime. */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraSystemEditWindow
{
public:
	static const FName TabId;
	static void RegisterTabSpawner();
	static void UnregisterTabSpawner();
	static void OpenLiveEditing(AComposableCameraCameraBase* Camera);

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
