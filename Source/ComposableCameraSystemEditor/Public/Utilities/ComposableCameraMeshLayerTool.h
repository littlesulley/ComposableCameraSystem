// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

class FComposableCameraMeshLayerTool
{
public:
	static void Register();
	static void Unregister();

private:
	static void RegisterMenus();
	static void ToggleEditMode();
	static void TogglePreviewMode();
	static bool IsEditModeActive();
	static bool IsPreviewModeActive();
};
