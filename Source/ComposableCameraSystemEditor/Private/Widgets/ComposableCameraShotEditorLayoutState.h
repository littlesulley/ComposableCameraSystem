// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

namespace ComposableCameraSystem::ShotEditorLayout
{
	struct FShotEditorLayoutState
	{
		bool bViewportToolbarCollapsed = false;
		bool bQuickControlsCollapsed = true;
	};

	inline FShotEditorLayoutState ResolveLayoutState(
		const TOptional<bool>& ViewportToolbarCollapsed,
		const TOptional<bool>& QuickControlsCollapsed)
	{
		FShotEditorLayoutState State;

		if (ViewportToolbarCollapsed.IsSet())
		{
			State.bViewportToolbarCollapsed = ViewportToolbarCollapsed.GetValue();
		}
		if (QuickControlsCollapsed.IsSet())
		{
			State.bQuickControlsCollapsed = QuickControlsCollapsed.GetValue();
		}

		return State;
	}
}
