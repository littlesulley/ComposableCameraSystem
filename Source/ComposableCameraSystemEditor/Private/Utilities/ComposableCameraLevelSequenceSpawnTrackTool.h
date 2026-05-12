// Copyright Sulley. All Rights Reserved.

#pragma once

/**
 * Editor-only Level Sequence helper that derives CCS Spawn Track keys from the
 * focused sequence's Camera Cut sections.
 */
class FComposableCameraLevelSequenceSpawnTrackTool
{
public:
	static void Register();
	static void Unregister();

	static bool CanKeySpawnTracksFromCameraCuts();
	static bool KeySpawnTracksFromCameraCuts();

private:
	static void RegisterMenus();
};
