// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SShotEditorViewport.h"

namespace ComposableCameraSystem::ShotEditorModeSwitch
{
	enum class EModeRequestHandling: uint8
	{
		Ignore,
		ApplyImmediately,
		ShowFreeExitStatus
	};

	inline EModeRequestHandling ClassifyModeRequest(
		EShotEditorMode CurrentMode,
		EShotEditorMode RequestedMode)
	{
		if (CurrentMode == RequestedMode)
		{
			return EModeRequestHandling::Ignore;
		}
		if (CurrentMode == EShotEditorMode::Free
			&& RequestedMode != EShotEditorMode::Free)
		{
			return EModeRequestHandling::ShowFreeExitStatus;
		}
		return EModeRequestHandling::ApplyImmediately;
	}

	inline bool ShouldShowFreeExitStatus(
		bool bHasPendingFreeExitMode,
		EShotEditorMode CurrentMode)
	{
		return bHasPendingFreeExitMode && CurrentMode == EShotEditorMode::Free;
	}
}
