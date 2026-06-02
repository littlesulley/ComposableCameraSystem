// Copyright 2026 Sulley. All Rights Reserved.

#include "Utils/ComposableCameraViewportUtils.h"

#include "CineCameraComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "EditorHooks/EditorHooks.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"

namespace UE::ComposableCameras
{
	bool TryGetEffectiveViewportSize(
		const AComposableCameraPlayerCameraManager* OptionalPCM,
		FIntPoint& OutSize)
	{
		// 1. PCM->PlayerController ->viewport (runs when the camera is driven
		//    by a real PlayerCameraManager; handles split-screen per-player
		//    viewports correctly because GetViewportSize on the PC goes
		//    through ULocalPlayer's GetProjectionData).
		if (OptionalPCM)
		{
			if (APlayerController* PC = OptionalPCM->GetOwningPlayerController())
			{
				int32 X = 0;
				int32 Y = 0;
				PC->GetViewportSize(X, Y);
				if (X > 0 && Y > 0)
				{
					OutSize = FIntPoint(X, Y);
					return true;
				}
			}
		}

		// 2. GameViewport (game worlds without a PCM, or a PCM whose owning
		//    controller isn't wired yet: PIE, standalone, packaged game).
		if (GEngine && GEngine->GameViewport)
		{
			FVector2D Size = FVector2D::ZeroVector;
			GEngine->GameViewport->GetViewportSize(Size);
			if (Size.X > 0.0 && Size.Y > 0.0)
			{
				OutSize = FIntPoint(FMath::FloorToInt(Size.X), FMath::FloorToInt(Size.Y));
				return true;
			}
		}

		// 3. Editor-world active viewport (LS Spawnable preview, scrubbing
		//    the Sequencer in editor without entering PIE). Routed through
		//    the `FGetActiveEditorViewport` hook so the runtime module stays
		//    editor-clean. The editor module binds the delegate at startup
		//    to `GEditor->GetActiveViewport()->GetSizeXY()`.
		{
			FIntPoint EditorSize = FIntPoint::ZeroValue;
			if (FGetActiveEditorViewport::TryGetSize(EditorSize)
				&& EditorSize.X > 0 && EditorSize.Y > 0)
			{
				OutSize = EditorSize;
				return true;
			}
		}

		// 4. Last-resort fallback: 1920x1080. Keeps AspectRatio math benign
		//    (16:9) when nothing is available. Extremely-early-startup or
		//    headless commandlet paths where neither game nor editor
		//    viewports exist.
		OutSize = FIntPoint(1920, 1080);
		return false;
	}

	float GetEffectiveViewportAspectRatio(const AComposableCameraPlayerCameraManager* OptionalPCM)
	{
		FIntPoint Size;
		TryGetEffectiveViewportSize(OptionalPCM, Size);
		// Size is always valid (fallback path guarantees 1920x1080), so no
		// divide-by-zero risk.
		return static_cast<float>(Size.X) / static_cast<float>(Size.Y);
	}

	float GetEffectiveAspectRatioForCineCamera(
		const UCineCameraComponent* CineCam,
		const AComposableCameraPlayerCameraManager* OptionalPCM)
	{
		if (!CineCam)
		{
			return GetEffectiveViewportAspectRatio(OptionalPCM);
		}
		// Constrained: renderer letterboxes to the filmback-derived aspect,
		// regardless of viewport. Solver should match exactly so anchor
		// screen positions land where designers expect.
		if (CineCam->bConstrainAspectRatio)
		{
			const float CamAspect = CineCam->AspectRatio;
			if (CamAspect > 0.f)
			{
				return CamAspect;
			}
		}
		// Unconstrained: renderer adapts to viewport. Use the actual viewport
		// aspect. Same source as the non-CineCam path. When designers resize
		// the level viewport, anchor screen positions track in real time.
		return GetEffectiveViewportAspectRatio(OptionalPCM);
	}
}
