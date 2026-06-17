// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SShotEditorViewport.h"

namespace ComposableCameraSystem::ShotEditorViewportToolbar
{
	enum class EViewportToolbarAction: uint8
	{
		ResetView,
		ToggleDiagnosticHud,
		ToggleCompositionGuides
	};

	inline bool IsToolbarActionEnabled(EViewportToolbarAction Action,
		bool bViewportAvailable,
		bool bHasActiveShot,
		EShotEditorMode ViewportMode)
	{
		if (!bViewportAvailable)
		{
			return false;
		}

		switch (Action)
		{
		case EViewportToolbarAction::ResetView:
			return bHasActiveShot && ViewportMode == EShotEditorMode::Free;
		case EViewportToolbarAction::ToggleDiagnosticHud:
		case EViewportToolbarAction::ToggleCompositionGuides:
			return bHasActiveShot;
		}

		return false;
	}

	inline bool ShouldShowToolbarExpandedControls(bool bToolbarCollapsed)
	{
		return !bToolbarCollapsed;
	}
}
