// Copyright Sulley. All rights reserved.

#include "Debug/ComposableCameraShotZoneOverlay.h"

#if !UE_BUILD_SHIPPING

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Math/ComposableCameraMath.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "SceneView.h"

namespace
{
	// ─── CVar ─────────────────────────────────────────────────────────────
	static TAutoConsoleVariable<int32> CVarShowShotZones(
		TEXT("CCS.Debug.Viewport.ShotZones"),
		0,
		TEXT("Show Cinemachine-style framing-zone overlay in the LS / PIE / Game\n")
		TEXT("viewport for every active CompositionFramingNode whose Aim or\n")
		TEXT("Placement zones are enabled. Independent of `CCS.Debug.Viewport`\n")
		TEXT("(the 3D-gizmo master switch).\n")
		TEXT("0 = off (default), 1 = on."),
		ECVF_Default);

	// ─── Visual constants — kept consistent with the Shot Editor overlay ──
	constexpr float kEdgeHighlightThickness = 2.5f;

	const FLinearColor kAnchorColorPlacement(1.f, 0.85f, 0.2f, 1.f);
	const FLinearColor kAnchorColorAim      (0.4f, 0.8f, 1.f, 1.f);

	const FLinearColor kZoneDeadFillPlacement(1.f,  0.85f, 0.2f, 0.18f);
	const FLinearColor kZoneSoftFillPlacement(1.f,  0.85f, 0.2f, 0.08f);
	const FLinearColor kZoneDeadFillAim      (0.4f, 0.8f,  1.f,  0.18f);
	const FLinearColor kZoneSoftFillAim      (0.4f, 0.8f,  1.f,  0.08f);

	constexpr float kAnchorDiscRadius = 6.f;

	// ─── Dedup state — see ComposableCameraDebugPanel.cpp big comment ────
	FDelegateHandle GZoneOverlayGameHandle;
	FDelegateHandle GZoneOverlayEditorHandle;

	uint64         GLastDrawFrame  = 0;
	const FCanvas* GLastDrawCanvas = nullptr;

	// ─────────────────────────────────────────────────────────────────────
	// Drawing helpers
	// ─────────────────────────────────────────────────────────────────────

	/** Convert normalized screen `[-0.5, 0.5]²` (solver convention, +Y up)
	 *  to viewport pixel coords (top-left origin, +Y down). */
	FORCEINLINE FVector2D NormalizedScreenToPixel(const FVector2D& Norm, float SizeX, float SizeY)
	{
		return FVector2D(
			(Norm.X + 0.5f) * SizeX,
			(0.5f - Norm.Y) * SizeY);
	}

	/** Translucent rect tile. No-op when size is non-positive (degenerate
	 *  zone with matching dead/soft sides — e.g. SoftLeft == DeadLeft on
	 *  X axis collapses the left strip of the soft "ring"). */
	void DrawFilledTile(FCanvas& Canvas, const FBox2D& Box, const FLinearColor& Color)
	{
		if (Box.GetSize().X <= 0.f || Box.GetSize().Y <= 0.f)
		{
			return;
		}
		FCanvasTileItem Tile(
			Box.Min,
			FVector2D(Box.GetSize().X, Box.GetSize().Y),
			Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(Tile);
	}

	/** Draw the four edges of `Rect` as thin lines. Used as the resting
	 *  border for the LS overlay (unlike the Shot Editor preview, which
	 *  has no resting border; the LS viewport is denser scenery so the
	 *  zone reads better with a faint outline). */
	void DrawRectBorder(FCanvas& Canvas, const FBox2D& Rect, const FLinearColor& Color, float Thickness = 1.f)
	{
		const FVector2D TL = Rect.Min;
		const FVector2D TR = FVector2D(Rect.Max.X, Rect.Min.Y);
		const FVector2D BR = Rect.Max;
		const FVector2D BL = FVector2D(Rect.Min.X, Rect.Max.Y);
		auto DrawSeg = [&](const FVector2D& A, const FVector2D& B)
		{
			FCanvasLineItem L(A, B);
			L.SetColor(Color);
			L.LineThickness = Thickness;
			Canvas.DrawItem(L);
		};
		DrawSeg(TL, TR);
		DrawSeg(TR, BR);
		DrawSeg(BR, BL);
		DrawSeg(BL, TL);
	}

	/** Draw one anchor's overlay (zones + anchor disc) into the current
	 *  Canvas. `AnchorWorldPos` was already validated to resolve. */
	void DrawAnchorOverlay(
		FCanvas& Canvas,
		float SizeX, float SizeY,
		const FVector& CamPos, const FRotator& CamRot, float TanHalfHOR,
		const FVector& AnchorWorldPos,
		const FVector2D& AuthoredScreenPos,
		const FShotScreenZones* Zones,
		const FLinearColor& AnchorColor,
		const FLinearColor& DeadFill,
		const FLinearColor& SoftFill)
	{
		// V2.2: the main disc represents the AUTHORED ScreenPosition
		// (the designer's "I want the anchor here" target). The anchor's
		// LIVE projected position is shown as a smaller, read-only
		// marker, only when zones are enabled (zones-off keeps projection
		// ≈ SP and the marker would just double-stamp the disc).
		FVector2D AnchorNorm;
		const float Aspect = (SizeY > 0.f) ? (SizeX / SizeY) : (16.f / 9.f);
		const bool bAnchorInFront = ComposableCameraSystem::ProjectWorldPointToScreen(
			AnchorWorldPos, CamPos, CamRot, TanHalfHOR, Aspect, AnchorNorm);

		// Zones (when enabled) center on the AUTHORED ScreenPosition (the
		// solver's invariant — anchor floats inside the zone, zone tracks
		// SP). Solver conventions match the Shot Editor preview, so the
		// pixel-space rect math is identical.
		if (Zones && Zones->bEnabled)
		{
			const FVector2D ZoneCenterPx = NormalizedScreenToPixel(AuthoredScreenPos, SizeX, SizeY);

			const float DeadL = Zones->DeadZone.Left   * SizeX;
			const float DeadR = Zones->DeadZone.Right  * SizeX;
			const float DeadT = Zones->DeadZone.Top    * SizeY;
			const float DeadB = Zones->DeadZone.Bottom * SizeY;
			const float SoftL = FMath::Max(Zones->SoftZone.Left,   Zones->DeadZone.Left)   * SizeX;
			const float SoftR = FMath::Max(Zones->SoftZone.Right,  Zones->DeadZone.Right)  * SizeX;
			const float SoftT = FMath::Max(Zones->SoftZone.Top,    Zones->DeadZone.Top)    * SizeY;
			const float SoftB = FMath::Max(Zones->SoftZone.Bottom, Zones->DeadZone.Bottom) * SizeY;

			const FBox2D DeadRect(
				FVector2D(ZoneCenterPx.X - DeadL, ZoneCenterPx.Y - DeadT),
				FVector2D(ZoneCenterPx.X + DeadR, ZoneCenterPx.Y + DeadB));
			const FBox2D SoftRect(
				FVector2D(ZoneCenterPx.X - SoftL, ZoneCenterPx.Y - SoftT),
				FVector2D(ZoneCenterPx.X + SoftR, ZoneCenterPx.Y + SoftB));

			// Soft "ring" — four tiles around the dead rect.
			DrawFilledTile(Canvas, FBox2D(
				FVector2D(SoftRect.Min.X, SoftRect.Min.Y),
				FVector2D(SoftRect.Max.X, DeadRect.Min.Y)), SoftFill);
			DrawFilledTile(Canvas, FBox2D(
				FVector2D(SoftRect.Min.X, DeadRect.Max.Y),
				FVector2D(SoftRect.Max.X, SoftRect.Max.Y)), SoftFill);
			DrawFilledTile(Canvas, FBox2D(
				FVector2D(SoftRect.Min.X, DeadRect.Min.Y),
				FVector2D(DeadRect.Min.X, DeadRect.Max.Y)), SoftFill);
			DrawFilledTile(Canvas, FBox2D(
				FVector2D(DeadRect.Max.X, DeadRect.Min.Y),
				FVector2D(SoftRect.Max.X, DeadRect.Max.Y)), SoftFill);

			// Dead inner — intentionally NOT filled (matches the Shot
			// Editor preview behavior). The soft ring frames the area
			// without obscuring the subject; designers see "the hold zone
			// is empty" as the visual signal that the camera ignores the
			// anchor inside it. The faint outline below still draws the
			// dead-rect border so the boundary is locatable.

			// Faint outline. The LS viewport is busy compared to the Shot
			// Editor's empty preview, so a 1px outline (alpha-matched to
			// the fill hue at 35%) is added for legibility — without it,
			// the soft ring's 8% alpha can blend into varied scenery and
			// the zone boundary becomes hard to read.
			const FLinearColor Outline(AnchorColor.R, AnchorColor.G, AnchorColor.B, 0.35f);
			DrawRectBorder(Canvas, SoftRect, Outline);
			DrawRectBorder(Canvas, DeadRect, Outline);

			// Live-position marker — anchor's projected pixel. Read-only
			// indicator showing where the anchor IS this frame, distinct
			// from the SP disc (drawn below) which shows where the solver
			// is trying to put it. Inside the dead zone the two drift
			// apart; outside they converge. Skip when projection is
			// behind the camera — the disc at SP is enough on its own.
			if (bAnchorInFront)
			{
				const FVector2D ProjPx = NormalizedScreenToPixel(AnchorNorm, SizeX, SizeY);
				constexpr float kProjRingRadius = 5.f;
				constexpr float kProjCrossArm   = 4.f;
				const FLinearColor ProjColor(
					AnchorColor.R, AnchorColor.G, AnchorColor.B, 0.55f);

				constexpr int32 kProjRingSegs = 16;
				FVector2D PrevPt(ProjPx.X + kProjRingRadius, ProjPx.Y);
				for (int32 s = 1; s <= kProjRingSegs; ++s)
				{
					const float T = (s / static_cast<float>(kProjRingSegs)) * 2.f * PI;
					const FVector2D Pt(
						ProjPx.X + FMath::Cos(T) * kProjRingRadius,
						ProjPx.Y + FMath::Sin(T) * kProjRingRadius);
					FCanvasLineItem Seg(PrevPt, Pt);
					Seg.SetColor(ProjColor); Seg.LineThickness = 1.f;
					Canvas.DrawItem(Seg);
					PrevPt = Pt;
				}
				FCanvasLineItem H(
					FVector2D(ProjPx.X - kProjCrossArm, ProjPx.Y),
					FVector2D(ProjPx.X + kProjCrossArm, ProjPx.Y));
				H.SetColor(ProjColor); H.LineThickness = 1.f;
				Canvas.DrawItem(H);
				FCanvasLineItem V(
					FVector2D(ProjPx.X, ProjPx.Y - kProjCrossArm),
					FVector2D(ProjPx.X, ProjPx.Y + kProjCrossArm));
				V.SetColor(ProjColor); V.LineThickness = 1.f;
				Canvas.DrawItem(V);
			}
		}

		// Main anchor disc — drawn at the AUTHORED ScreenPosition. This
		// is the "ideal" target the solver is converging the anchor
		// toward. With zones disabled the anchor projection is on top of
		// this point too; with zones on, the live-position marker above
		// reveals the drift.
		{
			const FVector2D SPPx = NormalizedScreenToPixel(AuthoredScreenPos, SizeX, SizeY);
			FCanvasNGonItem Disc(SPPx, FVector2D(kAnchorDiscRadius, kAnchorDiscRadius), 16, AnchorColor);
			Canvas.DrawItem(Disc);
		}
	}

	/** Resolve world point for an anchor spec, returning false on failure
	 *  so the caller can skip the corresponding overlay piece. */
	bool TryResolveAnchorWorld(
		const FComposableCameraAnchorSpec& Anchor,
		TConstArrayView<FComposableCameraShotTarget> Targets,
		FVector& OutWorld)
	{
		return Anchor.ResolveWorldPosition(Targets, OutWorld);
	}

	/** Per-FramingNode draw entry. Emits at most two overlays — Placement
	 *  (yellow) and Aim (cyan) — but only the ones whose mode actually
	 *  reads the corresponding screen position. */
	void DrawShotOverlay(
		FCanvas& Canvas,
		float SizeX, float SizeY,
		const FVector& CamPos, const FRotator& CamRot, float TanHalfHOR,
		const FComposableCameraShot& Shot)
	{
		// Placement anchor: only meaningful in `AnchorAtScreen` (the only
		// mode that authors `Placement.ScreenPosition`). Other modes get
		// no overlay even if PlacementZones.bEnabled is true — drawing it
		// would imply an effect the solver won't honor.
		if (Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen)
		{
			FVector AnchorWorld;
			if (TryResolveAnchorWorld(Shot.Placement.PlacementAnchor, Shot.Targets, AnchorWorld))
			{
				DrawAnchorOverlay(
					Canvas, SizeX, SizeY,
					CamPos, CamRot, TanHalfHOR,
					AnchorWorld,
					Shot.Placement.ScreenPosition,
					&Shot.Placement.PlacementZones,
					kAnchorColorPlacement,
					kZoneDeadFillPlacement,
					kZoneSoftFillPlacement);
			}
		}

		// Aim anchor: meaningful for `LookAtAnchor`. NoOp doesn't read
		// `Aim.ScreenPosition`, so suppress to avoid misleading visuals.
		if (Shot.Aim.Mode == EShotAimMode::LookAtAnchor)
		{
			FVector AnchorWorld;
			if (TryResolveAnchorWorld(Shot.Aim.AimAnchor, Shot.Targets, AnchorWorld))
			{
				DrawAnchorOverlay(
					Canvas, SizeX, SizeY,
					CamPos, CamRot, TanHalfHOR,
					AnchorWorld,
					Shot.Aim.ScreenPosition,
					&Shot.Aim.AimZones,
					kAnchorColorAim,
					kZoneDeadFillAim,
					kZoneSoftFillAim);
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────
	// UDebugDrawService callback
	// ─────────────────────────────────────────────────────────────────────

	void DrawZoneOverlay_Inner(UCanvas* UCanvasObj, APlayerController* PC)
	{
		if (CVarShowShotZones.GetValueOnGameThread() == 0)
		{
			return;
		}
		if (!UCanvasObj || !UCanvasObj->Canvas)
		{
			return;
		}
		FCanvas& Canvas = *UCanvasObj->Canvas;

		// On-screen diagnostic readout — top-right corner. Confirms the
		// overlay callback is firing AND surfaces the active-instance
		// count. Resolves the four likely "I see nothing" causes at a
		// glance:
		//   - No readout at all  ⇒ overlay not registered / CVar misread.
		//   - "Active=0"         ⇒ FramingNode never registered (no LS
		//                          shot active, or InitializeNodes path
		//                          didn't fire).
		//   - "Active=N, PC=..." ⇒ overlay path works; problem is in the
		//                          per-anchor draw (mode gating, anchor
		//                          unresolvable, or off-screen).
		// Also draws a small red crosshair at canvas center as a "render
		// path verified" signal that doesn't depend on any Shot data.
		if (UFont* DiagFont = GEngine ? GEngine->GetSmallFont() : nullptr)
		{
			const int32 SetSize = UComposableCameraCompositionFramingNode::GetActiveInstances().Num();
			const FString Diag = FString::Printf(
				TEXT("ShotZones: Active=%d  PC=%s  Canvas=%dx%d"),
				SetSize,
				PC ? *PC->GetName() : TEXT("<none>"),
				UCanvasObj->SizeX, UCanvasObj->SizeY);
			FCanvasTextItem DiagItem(
				FVector2D(8.f, 8.f),
				FText::FromString(Diag),
				DiagFont,
				FLinearColor(1.f, 0.5f, 0.5f, 1.f));
			DiagItem.EnableShadow(FLinearColor::Black);
			Canvas.DrawItem(DiagItem);
		}
		// Center crosshair — pure-render heartbeat, independent of Shot
		// data. If the user sees the crosshair but no zone fills, the
		// Canvas path works and the bug is in the FramingNode iteration
		// or the per-anchor draw.
		{
			const FVector2D Center(UCanvasObj->SizeX * 0.5f, UCanvasObj->SizeY * 0.5f);
			const FLinearColor Red(1.f, 0.2f, 0.2f, 1.f);
			FCanvasLineItem H(Center - FVector2D(8.f, 0.f), Center + FVector2D(8.f, 0.f));
			H.SetColor(Red);  H.LineThickness = 2.f;
			Canvas.DrawItem(H);
			FCanvasLineItem V(Center - FVector2D(0.f, 8.f), Center + FVector2D(0.f, 8.f));
			V.SetColor(Red);  V.LineThickness = 2.f;
			Canvas.DrawItem(V);
		}

		// Resolve the current viewport's view info — preferred source is
		// the supplied PlayerController's PCM (PIE / Game / Standalone).
		// In editor channels without a PC bound, fall back to whatever
		// the canvas is rendering for (e.g. Sequencer preview viewport).
		FMinimalViewInfo VI;
		if (PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->GetCameraViewPoint(VI.Location, VI.Rotation);
			VI.FOV = PC->PlayerCameraManager->GetFOVAngle();
		}
		else
		{
			// Editor-channel without PC: use the canvas's scene view if
			// available. `UCanvas::SceneView` is set by UDebugDrawService
			// per draw call. Skip when the scene view isn't there — the
			// "no game world" case (unrelated editor viewport firing the
			// channel) can't possibly need this overlay.
			const FSceneView* SceneView = UCanvasObj->SceneView;
			if (!SceneView)
			{
				return;
			}
			VI.Location = SceneView->ViewMatrices.GetViewOrigin();
			VI.Rotation = SceneView->ViewMatrices.GetViewMatrix().InverseFast().Rotator();
			// FOV from the view's projection matrix — half-angle stored
			// at index [0][0] = 1 / tan(HFOV/2). Cheap reverse.
			const float Proj00 = static_cast<float>(SceneView->ViewMatrices.GetProjectionMatrix().M[0][0]);
			const float TanHalfH = (FMath::Abs(Proj00) > UE_KINDA_SMALL_NUMBER) ? (1.f / Proj00) : 1.f;
			VI.FOV = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Abs(TanHalfH)));
		}

		const float SizeX = static_cast<float>(UCanvasObj->SizeX);
		const float SizeY = static_cast<float>(UCanvasObj->SizeY);
		if (SizeX <= 0.f || SizeY <= 0.f)
		{
			return;
		}
		const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(VI.FOV * 0.5f));

		// Iterate every active framing node. Stale weak ptrs (GC'd nodes
		// that didn't BeginDestroy in time, e.g. forced GC) silently drop
		// out via the `Get()` null check.
		for (const TWeakObjectPtr<UComposableCameraCompositionFramingNode>& Weak
			: UComposableCameraCompositionFramingNode::GetActiveInstances())
		{
			const UComposableCameraCompositionFramingNode* Node = Weak.Get();
			if (!Node)
			{
				continue;
			}
			DrawShotOverlay(Canvas, SizeX, SizeY, VI.Location, VI.Rotation, TanHalfHOR, Node->Shot);
		}
	}

	/** Dedup wrapper: same (frame, FCanvas*) key the debug panel uses to
	 *  avoid double-draw when one viewport transiently has both Game +
	 *  Editor ShowFlags set, while still drawing once per viewport per
	 *  frame in normal F8-eject scenarios. */
	void DrawZoneOverlay_Dedup(UCanvas* Canvas, APlayerController* PC)
	{
		const uint64 Frame = GFrameCounter;
		const FCanvas* CanvasKey = (Canvas != nullptr) ? Canvas->Canvas : nullptr;
		if (Frame == GLastDrawFrame && CanvasKey == GLastDrawCanvas)
		{
			return;
		}
		GLastDrawFrame = Frame;
		GLastDrawCanvas = CanvasKey;
		DrawZoneOverlay_Inner(Canvas, PC);
	}
} // anonymous namespace

void FComposableCameraShotZoneOverlay::Initialize()
{
	if (!GZoneOverlayGameHandle.IsValid())
	{
		GZoneOverlayGameHandle = UDebugDrawService::Register(
			TEXT("Game"),
			FDebugDrawDelegate::CreateStatic(&DrawZoneOverlay_Dedup));
	}
	if (!GZoneOverlayEditorHandle.IsValid())
	{
		GZoneOverlayEditorHandle = UDebugDrawService::Register(
			TEXT("Editor"),
			FDebugDrawDelegate::CreateStatic(&DrawZoneOverlay_Dedup));
	}
}

void FComposableCameraShotZoneOverlay::Shutdown()
{
	if (GZoneOverlayGameHandle.IsValid())
	{
		UDebugDrawService::Unregister(GZoneOverlayGameHandle);
		GZoneOverlayGameHandle.Reset();
	}
	if (GZoneOverlayEditorHandle.IsValid())
	{
		UDebugDrawService::Unregister(GZoneOverlayEditorHandle);
		GZoneOverlayEditorHandle.Reset();
	}
}

#else  // UE_BUILD_SHIPPING — overlay is debug-only

void FComposableCameraShotZoneOverlay::Initialize() {}
void FComposableCameraShotZoneOverlay::Shutdown()   {}

#endif
