// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FEditorViewportClient;

/**
 * Editor-only helper for copying viewport camera poses as importable FTransform
 * text and for keying that pose into selected CCS Level Sequence Transform tracks.
 * Used by the Level Editor global command and the Shot Editor preview viewport.
 */
class FComposableCameraViewportTransformClipboard
{
public:
	static void Register();
	static void Unregister();

	static bool CanCopyActiveLevelViewportCameraTransform();
	static bool CopyActiveLevelViewportCameraTransform();
	static bool CopyViewportCameraTransform(const FEditorViewportClient& ViewportClient, const FText& SourceLabel);

	static bool CanKeyActiveLevelViewportCameraTransformToSequencer();
	static bool KeyActiveLevelViewportCameraTransformToSequencer();
	static bool KeyViewportCameraTransformToSequencer(const FEditorViewportClient& ViewportClient, const FText& SourceLabel);

private:
	static void RegisterMenus();
};
