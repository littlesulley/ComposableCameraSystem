// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraViewportUtils.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
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
		// 1. PCM → PlayerController → viewport (runs when the camera is driven
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
		//    controller isn't wired yet — PIE, standalone, packaged game).
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

		// 3. Last-resort fallback: 1920×1080. Keeps AspectRatio math benign
		//    (16:9) when nothing is available — happens in the editor-world
		//    preview path where there is no GameViewport. If a reliable
		//    editor-viewport size becomes a requirement, the Editor module
		//    can add a separate helper that pulls from GEditor->GetActiveViewport()
		//    and route node code through that (not done here to keep the
		//    Runtime module editor-clean).
		OutSize = FIntPoint(1920, 1080);
		return false;
	}

	float GetEffectiveViewportAspectRatio(const AComposableCameraPlayerCameraManager* OptionalPCM)
	{
		FIntPoint Size;
		TryGetEffectiveViewportSize(OptionalPCM, Size);
		// Size is always valid (fallback path guarantees 1920×1080), so no
		// divide-by-zero risk.
		return static_cast<float>(Size.X) / static_cast<float>(Size.Y);
	}
}
