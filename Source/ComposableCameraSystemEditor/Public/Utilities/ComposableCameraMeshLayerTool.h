// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

class FComposableCameraMeshLayerTool
{
public:
	static void Register();
	static void Unregister();

	// Shared read-only preview action and live state for editor UI entry points.
	static void TogglePreviewMode();
	static bool IsPreviewModeActive();

private:
	friend class FComposableCameraMeshLayerEdMode;
	friend class FComposableCameraMeshLayerPreviewModeResumeTest;
	// Enter/Exit notifications also cover activation through the editor mode selector.
	static void NotifyEditModeChanged(bool bEntered);
	static void UpdateEditorPreviewMode();
	static void RegisterMenus();
	static void ToggleEditMode();
	static bool IsEditModeActive();
};
