// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

namespace UE::ComposableCameras::Mesh
{
	/** Every active Camera-bearing Layer owns one temporary Context. */
	inline bool ShouldCreateTemporaryCameraContext(
		bool bHasCameraType,
		bool bLayerAlreadyOwnsCameraContext)
	{
		return bHasCameraType && !bLayerAlreadyOwnsCameraContext;
	}

	/** Active Context pop resumes a lower camera already built without this Layer. */
	inline bool ShouldRefreshModifiersAfterLayerExit(
		bool bRemovedModifiers,
		bool bPoppedActiveCameraContext)
	{
		return bRemovedModifiers && !bPoppedActiveCameraContext;
	}

	/** Active pop preserves camera state, but candidate selection must release removed assets. */
	inline bool ShouldSyncModifierSelectionAfterLayerExit(
		bool bRemovedModifiers,
		bool bPoppedActiveCameraContext)
	{
		return bRemovedModifiers && bPoppedActiveCameraContext;
	}
}
