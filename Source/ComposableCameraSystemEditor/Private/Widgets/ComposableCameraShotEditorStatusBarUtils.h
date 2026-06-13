// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/ComposableCameraShotEditorModeSwitchUtils.h"
#include "Widgets/SShotEditorViewport.h"

namespace ComposableCameraSystem::ShotEditorStatusBar
{
	enum class EShotEditorStatusBarKind: uint8
	{
		Hidden,
		Info,
		Warning
	};

	enum class EShotEditorStatusBarActions: uint8
	{
		None,
		FreeExit
	};

	struct FShotEditorStatusBarState
	{
		EShotEditorStatusBarKind Kind = EShotEditorStatusBarKind::Hidden;
		EShotEditorStatusBarActions Actions = EShotEditorStatusBarActions::None;
	};

	inline FShotEditorStatusBarState ResolveStatusBarState(
		bool bHasActiveShot,
		bool bHostValid,
		bool bHasPendingFreeExitMode,
		EShotEditorMode CurrentMode)
	{
		if (bHasActiveShot && !bHostValid)
		{
			FShotEditorStatusBarState State;
			State.Kind = EShotEditorStatusBarKind::Warning;
			return State;
		}

		if (!bHasActiveShot)
		{
			FShotEditorStatusBarState State;
			State.Kind = EShotEditorStatusBarKind::Info;
			return State;
		}

		if (ShotEditorModeSwitch::ShouldShowFreeExitStatus(
				bHasPendingFreeExitMode, CurrentMode))
		{
			FShotEditorStatusBarState State;
			State.Kind = EShotEditorStatusBarKind::Warning;
			State.Actions = EShotEditorStatusBarActions::FreeExit;
			return State;
		}

		return FShotEditorStatusBarState();
	}

	inline bool ShouldShowStatusBar(const FShotEditorStatusBarState& State)
	{
		return State.Kind != EShotEditorStatusBarKind::Hidden;
	}
}
