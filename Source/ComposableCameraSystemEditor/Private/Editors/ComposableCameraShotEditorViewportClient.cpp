// Copyright Sulley. All Rights Reserved.

#include "Editors/ComposableCameraShotEditorViewportClient.h"

#include "Animation/SkeletalMeshActor.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "ComposableCameraSystemEditorModule.h"   // LogComposableCameraSystemEditor
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "ISequencer.h"
#include "Modules/ModuleManager.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequence.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotEditorViewportClient"
#include "Engine/Font.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/ComposableCameraMath.h"
#include "Math/ComposableCameraShotSolver.h"
#include "PreviewScene.h"
#include "SceneManagement.h"   // DrawWireBox, FPrimitiveDrawInterface
#include "SEditorViewport.h"

namespace
{
	/** UE's "BasicShapes" cylinder is 100uu × 100uu (XY × Z). To approximate
	 *  a 1.7m × 0.34m character capsule, scale (0.7, 0.7, 1.8) → 70uu wide,
	 *  180uu tall. Close enough as a stand-in for Targets that aren't backed
	 *  by a SkeletalMesh / StaticMesh source. */
	const FVector kCapsuleFallbackScale(0.7f, 0.7f, 1.8f);
	const TCHAR* const kCapsuleFallbackMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	// ─── Handle visuals (V2: dual-anchor) ────────────────────────────────
	constexpr float kHandleAnchorRadius      = 10.f;
	constexpr float kHandleHoverRadiusBoost  = 1.3f;
	constexpr float kHandleHitRadius         = 14.f;
	const FLinearColor kHandlePlacementColor (1.f,   0.85f, 0.2f,  1.f);   // yellow — Placement
	const FLinearColor kHandleAimColor       (0.4f,  0.8f,  1.f,   1.f);   // cyan — Aim
	const FLinearColor kHandleDisabledColor  (0.45f, 0.45f, 0.5f,  0.6f);  // greyed out
	const FLinearColor kHandleCrossColor     (0.05f, 0.05f, 0.05f, 1.f);   // dark cross overlay
}

FComposableCameraShotEditorViewportClient::FComposableCameraShotEditorViewportClient(
	FPreviewScene* InPreviewScene,
	const TSharedRef<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(/*InModeTools=*/nullptr, InPreviewScene, InEditorViewportWidget)
	, PreviewScene(InPreviewScene)
{
	// Q-B: Lit view mode + grid on + stats off (default Editor stats off anyway).
	SetViewMode(VMI_Lit);
	EngineShowFlags.SetGrid(true);

	// Realtime: solver-driven camera needs a tick every frame even without input.
	SetRealtime(true);

	// Force horizontal-FOV-locked aspect handling so the renderer's projection
	// stays in lockstep with the solver's math. Without this, the renderer
	// reads `ULevelEditorViewportSettings::AspectRatioAxisConstraint` (a global
	// editor preference, often `AspectRatio_MajorAxisFOV` by default) which
	// FLIPS the interpretation of `ViewFOV` between H-FOV and V-FOV when the
	// viewport's aspect crosses 1.0 (taller than wide → treats ViewFOV as
	// V-FOV; wider than tall → H-FOV). Our solver always treats `ViewFOV` as
	// H-FOV, so the flip causes pivot drift on viewport resize.
	//
	// The override is only honored when `bUseControllingActorViewInfo == true`
	// (per `EditorViewportClient.cpp:1043-1046`), so we have to opt into the
	// "controlling actor" code path. That path uses `ControllingActorViewInfo`
	// (an FMinimalViewInfo) for the projection matrix; ViewLocation / Rotation
	// still flow from `SetViewLocation` / `SetViewRotation` via the shared
	// `ViewTransform` member, so we keep using those. FOV however must be set
	// on `ControllingActorViewInfo.FOV` to participate (see Tick).
	bUseControllingActorViewInfo = true;
	ControllingActorViewInfo.bConstrainAspectRatio = false;
	ControllingActorAspectRatioAxisConstraint = AspectRatio_MaintainXFOV;

	// Post-process injection path. With bUseControllingActorViewInfo=true,
	// the engine routes post-process through
	// `ControllingActorViewInfo.PostProcessSettings` at weight
	// `ControllingActorViewInfo.PostProcessBlendWeight` (default 0 — i.e.,
	// no contribution unless we opt in). The virtual `OverridePostProcessSettings`
	// hook is the OTHER branch (only fires when bUseControllingActorViewInfo
	// is false), so we can't rely on it here. Set blend weight to 1 so our
	// per-frame DoF writes (RunSolverAndDriveCamera) actually take effect.
	ControllingActorViewInfo.PostProcessBlendWeight = 1.f;

	// Sane initial pose so the viewport isn't staring at the world origin
	// before any Shot is bound. Looks at the origin from a 3/4 angle.
	SetViewLocation(FVector(-400.f, -400.f, 200.f));
	SetViewRotation(FRotator(-15.f, 45.f, 0.f));

	// Default FOV — overridden by solver once a Shot is bound. Set both
	// `ViewFOV` (used by stat widgets / culling) and `ControllingActorViewInfo.FOV`
	// (used by the projection matrix because of `bUseControllingActorViewInfo`).
	ViewFOV = 79.f;
	ControllingActorViewInfo.FOV = 79.f;

	// Listener position is meaningless for a non-game preview.
	bSetListenerPosition = false;
}

FComposableCameraShotEditorViewportClient::~FComposableCameraShotEditorViewportClient()
{
	// Note: NOT clearing `Viewport` here — the SShotEditorViewport widget does
	// it in its own destructor BEFORE this destructor fires (see research Q7).
	//
	// Note: NOT calling DestroyProxies() here either — by the time this
	// destructor runs, the owning widget's PreviewScene member may already
	// be dead. The widget's destructor calls ReleaseSceneResources() while
	// the scene is still alive. Reaching this destructor with proxies still
	// in ProxyActors would mean someone built a client without the standard
	// widget host — defensive no-op rather than risk Proxy->Destroy() on a
	// dead world.
	ProxyActors.Reset();
}

void FComposableCameraShotEditorViewportClient::ReleaseSceneResources()
{
	DestroyProxies();
	// Drop ActiveShot too so any straggler tick before the destructor runs
	// (Slate paint between widget destructor and base destructor) can't try
	// to drive a now-orphaned scene.
	ActiveShot = nullptr;
	ActiveHost = nullptr;
	LastRebuiltHost = nullptr;
	bCachedDoFValid = false;

	// D.4: close any in-flight drag transaction. Idempotent — the destructor
	// handles cleanup if the editor was torn down mid-drag.
	DragTransaction.Reset();
	ActiveDragHandleType = EHandleType::None;
	CachedHandles.Reset();

	// V2.1 A.2: same cleanup for any in-flight Alt+RMB Roll drag.
	RollTransaction.Reset();
	bRollDragActive = false;
}

void FComposableCameraShotEditorViewportClient::SetMode(EShotEditorMode InMode)
{
	CurrentMode = InMode;
}

void FComposableCameraShotEditorViewportClient::SetActiveShot(
	FComposableCameraShot* InShot, UObject* InHost)
{
	ActiveShot = InShot;
	ActiveHost = InHost;

	// Don't rebuild here synchronously — Tick() will detect the host change
	// and rebuild on next frame. Avoids reentrancy if SetActiveShot is called
	// from a Slate paint pass.
}

void FComposableCameraShotEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Polish P.2 — invalidate the per-frame `BuildEffectiveShotForPreview`
	// cache at the start of each tick. Subsequent paint-time callers (HUD,
	// 3D wire BBs, handles, hover tooltip, etc.) within this frame share
	// a single override-resolution pass through the cache.
	bEffectiveShotCacheValid    = false;
	bEffectiveShotCacheBuiltOk  = false;

	// First-frame guard (research Q7) — Viewport is null until after the
	// SEditorViewport::Construct returns and the FSceneViewport is created.
	if (Viewport == nullptr)
	{
		return;
	}

	// Liveness guard: host UObject was destroyed (Type Asset closed, GC swept).
	// Drop the raw Shot pointer to avoid dangling reads.
	if (ActiveShot && !ActiveHost.IsValid())
	{
		ActiveShot = nullptr;
		DestroyProxies();
		LastRebuiltHost = nullptr;
		bCachedDoFValid = false;
		return;
	}

	if (!ActiveShot)
	{
		// No Shot to drive camera with — leave camera where the user (or
		// the default ctor pose) put it. Proxies were cleared on detach.
		bCachedDoFValid = false;
		return;
	}

	// Rebuild proxies on host change OR Targets count drift OR per-target
	// source actor identity change. The third check catches the
	// "designer just picked an Actor for an existing Target" path — count
	// is unchanged and host is unchanged, but the cylinder fallback proxy
	// from the null-actor moment needs to upgrade to SkelMesh / StaticMesh.
	const bool bHostChanged = (LastRebuiltHost != ActiveHost);
	const bool bCountDrift  = (ProxyActors.Num() != ActiveShot->Targets.Num());
	bool bSourceDrift = false;
	if (!bHostChanged && !bCountDrift)
	{
		const int32 N = ActiveShot->Targets.Num();
		if (LastResolvedSources.Num() != N)
		{
			bSourceDrift = true;
		}
		else
		{
			for (int32 i = 0; i < N; ++i)
			{
				const AActor* Now  = ResolveSourceActorForTargetIndex(i);
				const AActor* Then = LastResolvedSources[i].Get();
				if (Now != Then)
				{
					bSourceDrift = true;
					break;
				}
			}
		}
	}
	if (bHostChanged || bCountDrift || bSourceDrift)
	{
		RebuildProxies();
	}

	SyncProxyTransforms();

	// Solver runs in ALL modes — lens parameters (FOV / Aperture /
	// FocusDistance) are always Shot-data-driven so designer's lens edits in
	// the Details panel take effect regardless of mode. Camera POSE is mode-
	// specific: Drag/Lock honor solver pose; Free preserves user mouse-driven
	// pose. RunSolverAndDriveCamera handles the per-mode dispatch internally.
	RunSolverAndDriveCamera();
}

void FComposableCameraShotEditorViewportClient::DrawCanvas(
	FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	// Diagnostic HUD — only renders when a Shot is bound. Lets the user
	// watch aspect / viewport / camera state live while resizing the splitter
	// or window. Catches "solver and renderer disagree on aspect" regressions
	// at a glance: if `aspect (live)` and `aspect (solver)` ever diverge,
	// or if `anchor NDC.X / Y` ever drifts away from the authored
	// `Aim.ScreenPosition * 2`, the bug is in our wiring.
	if (!ActiveShot)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const FIntPoint VPSize = InViewport.GetSizeXY();
	const float LiveAspect = (VPSize.Y > 0)
		? static_cast<float>(VPSize.X) / static_cast<float>(VPSize.Y)
		: 0.f;

	const FVector  CamPos = GetViewLocation();
	const FRotator CamRot = GetViewRotation();
	const float    FOV    = ViewFOV;

	// Resolve anchor world position via the same path the solver uses —
	// goes through the override-resolved EffectiveShot so HUD diagnostic
	// matches what RunSolverAndDriveCamera actually sees (otherwise an
	// override-driven section reads ActiveShot.Targets[*].Actor=None and
	// the HUD shows UNRESOLVED even though the camera is moving correctly).
	FVector AnchorWorldPos = FVector::ZeroVector;
	FComposableCameraShot HUDEffectiveShot;
	// HUD shows the AIM anchor (where the camera is looking) — that's the
	// hard rotation constraint and the most useful diagnostic. The
	// PlacementAnchor is also rendered as a 3D gizmo via DrawNodeDebug.
	const bool bAnchorOK =
		BuildEffectiveShotForPreview(HUDEffectiveShot)
		&& HUDEffectiveShot.Aim.AimAnchor.ResolveWorldPosition(
			HUDEffectiveShot.Targets, AnchorWorldPos);

	// Project AimAnchor through current camera state — should land at
	// (Aim.ScreenPosition.X, Aim.ScreenPosition.Y) in [-0.5, 0.5]² if
	// solver and renderer agree (within 1-2 frame solver-converge lag in
	// SolvedFromBoundsFit FOV mode).
	FVector2D AnchorProjScreen(NAN, NAN);
	bool      bAnchorInFront = false;
	if (bAnchorOK)
	{
		const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
		bAnchorInFront = ComposableCameraSystem::ProjectWorldPointToScreen(
			AnchorWorldPos, CamPos, CamRot, TanHalfHOR, LiveAspect, AnchorProjScreen);
	}

	// Layout — top-left, padding 8px. Line height ~14 for SmallFont.
	auto DrawLine = [&Canvas, Font](int32 LineIdx, const FString& Text, const FLinearColor& Color = FLinearColor(0.95f, 0.95f, 1.f, 1.f))
	{
		FCanvasTextItem Item(FVector2D(8.f, 8.f + LineIdx * 14.f),
			FText::FromString(Text), Font, Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Item);
	};

	const FLinearColor Gray(0.65f, 0.65f, 0.7f, 1.f);
	const FLinearColor Yellow(1.f, 0.95f, 0.55f, 1.f);

	int32 L = 0;
	DrawLine(L++, FString::Printf(
		TEXT("Aspect Ratio (Live): %.4f        Viewport: %d x %d"),
		LiveAspect, VPSize.X, VPSize.Y), Yellow);
	DrawLine(L++, FString::Printf(
		TEXT("Camera Position:     (%.1f, %.1f, %.1f)"),
		CamPos.X, CamPos.Y, CamPos.Z), Gray);
	DrawLine(L++, FString::Printf(
		TEXT("Camera Rotation:     Pitch = %.2f, Yaw = %.2f, Roll = %.2f"),
		CamRot.Pitch, CamRot.Yaw, CamRot.Roll), Gray);
	DrawLine(L++, FString::Printf(
		TEXT("Field of View:       %.3f deg"), FOV), Gray);

	if (bCachedDoFValid)
	{
		DrawLine(L++, FString::Printf(
			TEXT("Focus / Aperture:    %.1f cm  /  f/%.2f  (36mm sensor)"),
			CachedFocusDistance, CachedAperture), Gray);
	}
	else
	{
		DrawLine(L++,
			TEXT("Focus / Aperture:    No valid solve (depth-of-field not driven)."),
			Gray);
	}

	if (bAnchorOK)
	{
		DrawLine(L++, FString::Printf(
			TEXT("Aim Anchor (World):  (%.1f, %.1f, %.1f)"),
			AnchorWorldPos.X, AnchorWorldPos.Y, AnchorWorldPos.Z), Gray);

		const FVector2D AuthoredScreenPos = ActiveShot->Aim.ScreenPosition;
		const FLinearColor ScreenColor = bAnchorInFront ? Yellow : FLinearColor(1.f, 0.4f, 0.4f, 1.f);

		if (bAnchorInFront)
		{
			const FVector2D Drift = AnchorProjScreen - AuthoredScreenPos;
			DrawLine(L++, FString::Printf(
				TEXT("Aim Anchor (Screen): Projected = (%.4f, %.4f)   Authored = (%.4f, %.4f)   Drift = (%.4f, %.4f)"),
				AnchorProjScreen.X, AnchorProjScreen.Y,
				AuthoredScreenPos.X, AuthoredScreenPos.Y,
				Drift.X, Drift.Y), ScreenColor);
		}
		else
		{
			DrawLine(L++, FString::Printf(
				TEXT("Aim Anchor (Screen): Behind camera   Authored = (%.4f, %.4f)"),
				AuthoredScreenPos.X, AuthoredScreenPos.Y), ScreenColor);
		}
	}
	else
	{
		DrawLine(L++,
			TEXT("Aim Anchor:          Unresolved."),
			FLinearColor(1.f, 0.4f, 0.4f, 1.f));
	}

	// E.2: bottom-left "current Shot" summary strip — single line of the
	// values designers iterate on most often (Mode + Distance + FOV + Roll),
	// so the Details panel doesn't need to be open during rapid Drag-mode
	// authoring. Distinguished from the top-left diagnostic HUD by position
	// and by being a compact one-liner rather than per-field rows.
	{
		const TCHAR* ModeLabel =
			CurrentMode == EShotEditorMode::Drag ? TEXT("Drag") :
			CurrentMode == EShotEditorMode::Free ? TEXT("Free") :
			TEXT("Lock");

		// Distance is mode-relevant only in modes that read it; FixedWorldPosition
		// ignores the field, so showing a number there would be misleading. The
		// wheel handler (§23.13) and reverse-solve already gate on the same
		// condition.
		const bool bDistanceUsed =
			ActiveShot->Placement.Mode != EShotPlacementMode::FixedWorldPosition;
		const FString DistanceField = bDistanceUsed
			? FString::Printf(TEXT("Distance: %.1f cm"), ActiveShot->Placement.Distance)
			: FString(TEXT("Distance: -"));

		const FString StripText = FString::Printf(
			TEXT("Mode: %s   |   %s   |   FOV: %.1f deg   |   Roll: %.1f deg"),
			ModeLabel, *DistanceField, FOV, ActiveShot->Roll);

		const float StripY = static_cast<float>(VPSize.Y) - 14.f - 8.f;
		FCanvasTextItem Strip(
			FVector2D(8.f, StripY),
			FText::FromString(StripText),
			Font, FLinearColor(0.55f, 0.95f, 1.f, 1.f));
		Strip.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Strip);
	}

	// D.4: draw screen-position handles on top of the HUD overlay.
	DrawHandles(InViewport, Canvas);
}

void FComposableCameraShotEditorViewportClient::Draw(
	const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (!ActiveShot || !PDI || !Viewport)
	{
		return;
	}

	// Bounds visualization is only meaningful when the solver actually
	// consumes bounds. Manual FOV mode reads `Lens.ManualFOV` directly and
	// ignores per-target bounds entirely, so drawing wireframes there is
	// pure visual noise — skip the whole pass.
	if (ActiveShot->Lens.FOVMode != EShotFOVMode::SolvedFromBoundsFit)
	{
		return;
	}

	// Build the same EffectiveShot the solver consumes, so binding-override
	// actors / freshly-resolved bounds are reflected in the wireframes.
	FComposableCameraShot EffectiveShot;
	if (!BuildEffectiveShotForPreview(EffectiveShot))
	{
		return;
	}

	// Refresh AutoFromComponentBounds caches on the local copy — same idiom
	// `RunSolverAndDriveCamera` uses (see §23.11 of EditorDesignDoc), so the
	// debug viz reflects the *exact* extents the solver will read this frame.
	for (FComposableCameraShotTarget& T : EffectiveShot.Targets)
	{
		if (T.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds)
		{
			T.RefreshAutoBoundsCache();
		}
	}

	// Replicate `SolvePerceptualUnionBoxFOV`'s 8-vertex `bAllOnScreen` filter
	// so the wire color tells the designer "is this box actually feeding the
	// FOV solve, or being silently dropped?".
	const FVector  CamPos = GetViewLocation();
	const FRotator CamRot = GetViewRotation();
	const FIntPoint VP    = Viewport->GetSizeXY();
	const float    Aspect = (VP.Y > 0)
		? static_cast<float>(VP.X) / static_cast<float>(VP.Y)
		: 16.f / 9.f;
	const float    TanH   = FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5f));

	const FLinearColor ColorContributing(0.2f, 0.9f,  0.3f, 1.f);   // green
	const FLinearColor ColorDroppedOffscreen(1.f, 0.85f, 0.2f, 1.f); // yellow
	constexpr float    Thickness = 1.5f;

	// Per-target selection for the bounds-fit solve uses the existing
	// `BoundsShape` + `BoundsContributionWeight` per-target authoring:
	//   - `BoundsShape == None`         → extent is zero, skip
	//   - `BoundsContributionWeight ≤ 0`→ explicitly opted out, skip
	//   - both pass                     → drawn (green if all 8 vertices in
	//                                     front of camera, yellow if any
	//                                     behind — solver's strict
	//                                     `bAllOnScreen` check would drop
	//                                     the target in the yellow case).
	// Designer "selects which actors contribute" by toggling those two
	// per-target fields; only the selected set draws here.
	for (const FComposableCameraShotTarget& T : EffectiveShot.Targets)
	{
		const FVector Extent = T.GetEffectiveBoundsExtent();
		if (Extent.IsZero())
		{
			continue;
		}
		if (T.BoundsContributionWeight <= 0.f)
		{
			continue;
		}

		FVector Pivot;
		if (!T.Target.ResolveWorldPoint(Pivot))
		{
			continue;
		}

		const FBox WorldBox(Pivot - Extent, Pivot + Extent);

		// Mirror the solver's strict 8-vertex check: a single vertex behind
		// the camera plane drops the entire target from the FOV solve.
		FVector Vertices[8];
		WorldBox.GetVertices(Vertices);
		bool bAllOnScreen = true;
		for (int32 v = 0; v < 8; ++v)
		{
			FVector2D Screen;
			if (!ComposableCameraSystem::ProjectWorldPointToScreen(
				Vertices[v], CamPos, CamRot, TanH, Aspect, Screen))
			{
				bAllOnScreen = false;
				break;
			}
		}
		const FLinearColor& Color = bAllOnScreen ? ColorContributing : ColorDroppedOffscreen;

		DrawWireBox(PDI, WorldBox, Color, SDPG_World, Thickness);
	}
}

// ─── D.4 implementation ──────────────────────────────────────────────────

FVector2D FComposableCameraShotEditorViewportClient::NormalizedScreenToPixel(
	const FVector2D& ScreenPos, const FIntPoint& VPSize) const
{
	// Solver convention: ScreenPos in [-0.5, 0.5]², +Y up.
	// Pixel convention:  origin top-left, +Y down.
	return FVector2D(
		(ScreenPos.X + 0.5f) * static_cast<float>(VPSize.X),
		(0.5f - ScreenPos.Y) * static_cast<float>(VPSize.Y));
}

FVector2D FComposableCameraShotEditorViewportClient::PixelToNormalizedScreen(
	int32 PixelX, int32 PixelY, const FIntPoint& VPSize) const
{
	const float W = static_cast<float>(FMath::Max(VPSize.X, 1));
	const float H = static_cast<float>(FMath::Max(VPSize.Y, 1));
	return FVector2D(
		static_cast<float>(PixelX) / W - 0.5f,
		0.5f - static_cast<float>(PixelY) / H);
}

void FComposableCameraShotEditorViewportClient::DrawHandles(
	FViewport& InViewport, FCanvas& Canvas)
{
	CachedHandles.Reset();
	if (!ActiveShot)
	{
		return;
	}

	const FIntPoint VPSize = InViewport.GetSizeXY();

	// Per-mode drawing rules:
	//   - All modes draw handles at the LIVE PROJECTION of each anchor's
	//     resolved world point through the current camera. This makes the
	//     handle "follow the anchor's actor" — changing
	//     `Placement.PlacementAnchor.TargetIndex` visibly moves the yellow
	//     handle to wherever the new target projects, even before the
	//     designer drags it.
	//   - Exception: while a handle is *actively being dragged*, it
	//     follows the cursor (which equals the just-written
	//     `Placement.ScreenPosition` / `Aim.ScreenPosition`) so the drag
	//     UX is responsive without a one-frame lag.
	//   - Drag mode: full color, hit-tested. Free / Lock: greyed out,
	//     non-interactive.
	const bool bInteractive = (CurrentMode == EShotEditorMode::Drag);
	const bool bDisabled    = !bInteractive;

	// Aim handle is non-effective when AimMode == NoOp — render greyed +
	// strip from the hit-test cache so dragging it can't write a value
	// the solver will then ignore. The handle stays *visible* so the
	// authored ScreenPosition is still readable; flipping AimMode back
	// to LookAtAnchor restores full interactivity.
	const bool bAimNoOp = (ActiveShot->Aim.Mode == EShotAimMode::NoOp);
	const bool bAimDisabled = bDisabled || bAimNoOp;
	const bool bAimInteractive = bInteractive && !bAimNoOp;

	// Placement handle is non-interactive when the active mode doesn't
	// consume `Placement.ScreenPosition` (i.e., AnchorOrbit pure mode and
	// FixedWorldPosition). Same UX shape as Aim+NoOp: handle stays visible
	// (live-projects the placement anchor) but greys out + drops from
	// hit-test so dragging can't write a silently-ignored value.
	const bool bPlacementUsesScreenPos =
		ActiveShot->Placement.Mode == EShotPlacementMode::AnchorAtScreen;
	const bool bPlacementDisabled    = bDisabled || !bPlacementUsesScreenPos;
	const bool bPlacementInteractive = bInteractive && bPlacementUsesScreenPos;

	UFont* HandleLabelFont = GEngine ? GEngine->GetSmallFont() : nullptr;

	// Pre-compute camera state for live projection.
	const FVector  CamPos = GetViewLocation();
	const FRotator CamRot = GetViewRotation();
	const float    LiveAspect = (VPSize.Y > 0)
		? static_cast<float>(VPSize.X) / static_cast<float>(VPSize.Y)
		: 16.f / 9.f;
	const float    TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5f));

	// Helper: draw filled circle + cross overlay at a given pixel pos.
	auto DrawHandleAt = [&Canvas](
		const FVector2D& PixelPos, float Radius, const FLinearColor& Fill, bool bWithCross)
	{
		FCanvasNGonItem Disc(PixelPos, FVector2D(Radius, Radius), 24, Fill);
		Canvas.DrawItem(Disc);

		if (bWithCross)
		{
			const float CrossArm = Radius * 0.55f;
			FCanvasLineItem H(
				FVector2D(PixelPos.X - CrossArm, PixelPos.Y),
				FVector2D(PixelPos.X + CrossArm, PixelPos.Y));
			H.SetColor(kHandleCrossColor);
			H.LineThickness = 1.5f;
			Canvas.DrawItem(H);

			FCanvasLineItem V(
				FVector2D(PixelPos.X, PixelPos.Y - CrossArm),
				FVector2D(PixelPos.X, PixelPos.Y + CrossArm));
			V.SetColor(kHandleCrossColor);
			V.LineThickness = 1.5f;
			Canvas.DrawItem(V);
		}
	};

	// Project the resolved anchor world point through the current camera.
	// `AnchorAccessor` extracts the relevant anchor from the override-
	// resolved EffectiveShot. Returns false (OutNorm untouched) when the
	// anchor can't resolve or the world point is behind camera — caller
	// falls back to the authored ScreenPosition in that case so the
	// handle stays visible during transient unresolvable states.
	auto ProjectAnchorWorld = [&](
		auto AnchorAccessor, FVector2D& OutNorm) -> bool
	{
		FComposableCameraShot EffectiveShot;
		FVector AnchorWorld;
		if (!BuildEffectiveShotForPreview(EffectiveShot))
		{
			return false;
		}
		const FComposableCameraAnchorSpec& Anchor = AnchorAccessor(EffectiveShot);
		if (!Anchor.ResolveWorldPosition(EffectiveShot.Targets, AnchorWorld))
		{
			return false;
		}
		FVector2D Projected;
		if (!ComposableCameraSystem::ProjectWorldPointToScreen(
			AnchorWorld, CamPos, CamRot, TanHalfHOR, LiveAspect, Projected))
		{
			return false;
		}
		OutNorm = Projected;
		return true;
	};

	auto DrawAnchorHandle = [&](
		EHandleType HandleType,
		const FVector2D& AuthoredScreenPos,
		auto AnchorAccessor,
		const FLinearColor& AnchorColor,
		const TCHAR* LabelText,
		bool bHandleDisabled,
		bool bHandleInteractive)
	{
		FVector2D NormPos;
		bool bHaveNormPos = false;
		const bool bActivelyDraggingThis =
			bHandleInteractive && (ActiveDragHandleType == HandleType);
		if (bActivelyDraggingThis)
		{
			// During a drag, ApplyDragToShot writes the cursor's normalized
			// position into the anchor's authored ScreenPosition every
			// CapturedMouseMove. Echoing that back here makes the handle
			// follow the cursor 1:1 without a one-frame projection lag.
			NormPos = AuthoredScreenPos;
			bHaveNormPos = true;
		}
		else
		{
			// Normal path: project the anchor's resolved world point so the
			// handle visibly tracks the actor referenced by the anchor.
			//
			// Visibility decisions when projection isn't usable as-is split
			// on whether the handle is interactive in the current mode:
			//
			//   - **Interactive** (Drag mode + handle's mode-config consumes
			//     `ScreenPosition`): keep the handle visible at the authored
			//     `ScreenPosition` so it stays clickable for re-aim. The
			//     authored value is the one the forward solver consumes, so
			//     drawing the handle there matches the field the designer
			//     would edit. Currently applies only to the
			//     anchor-unresolvable / behind-camera case; off-screen-but-
			//     in-front still extrapolates beyond the viewport (designer
			//     can't really click an invisible handle, but the existing
			//     behavior is preserved for now).
			//
			//   - **Non-interactive** (greyed in AnchorOrbit / NoOp / etc.):
			//     suppress the handle when the anchor isn't visible. The
			//     authored `ScreenPosition` is unread by the forward solver
			//     in these modes, so falling back to it would draw the
			//     handle at a stale value carried over from a different
			//     mode — misleading because designers can't act on a greyed
			//     handle anyway, and the position it lands at has no
			//     bearing on the rendered camera.
			bHaveNormPos = ProjectAnchorWorld(AnchorAccessor, NormPos);
			if (bHaveNormPos)
			{
				const bool bOnScreen =
					FMath::Abs(NormPos.X) <= 0.5f
					&& FMath::Abs(NormPos.Y) <= 0.5f;
				if (!bOnScreen && !bHandleInteractive)
				{
					return;
				}
			}
			else
			{
				if (!bHandleInteractive)
				{
					return;
				}
				NormPos = AuthoredScreenPos;
				bHaveNormPos = true;
			}
		}
		if (!bHaveNormPos)
		{
			return;
		}
		const FVector2D PixelPos = NormalizedScreenToPixel(NormPos, VPSize);
		const bool bHovered = bHandleInteractive
			&& HoveredHandleType == HandleType
			&& ActiveDragHandleType == EHandleType::None;
		const bool bDragging = ActiveDragHandleType == HandleType;
		const FLinearColor Fill = bHandleDisabled ? kHandleDisabledColor : AnchorColor;
		const float Radius = (bHovered || bDragging)
			? kHandleAnchorRadius * kHandleHoverRadiusBoost
			: kHandleAnchorRadius;
		DrawHandleAt(PixelPos, Radius, Fill, /*bWithCross=*/!bHandleDisabled);

		// Centered label above the disc — same hue family as the handle but
		// drops to the disabled grey when the handle is non-interactive so
		// the visual coupling between text + disc is preserved.
		if (HandleLabelFont && LabelText)
		{
			const FVector2D LabelPos(
				PixelPos.X,
				PixelPos.Y - Radius - 14.f);
			FCanvasTextItem Label(
				LabelPos, FText::FromString(LabelText),
				HandleLabelFont, Fill);
			Label.bCentreX = true;
			Label.EnableShadow(FLinearColor::Black);
			Canvas.DrawItem(Label);
		}

		// E.1: hover tooltip — when the cursor is over an interactive handle
		// (Drag mode + mode-config that consumes ScreenPosition), show
		// authored ScreenPosition + projected world position + Distance
		// (Placement only) below-right of the disc. Designer doesn't need
		// the Details panel open just to inspect anchor values during
		// rapid composition iteration. Resolves the anchor world position
		// inline because it's only computed on hover (rare, no hot-path
		// concern) — keeps the lambda free of extra threaded outputs.
		if (bHovered && HandleLabelFont)
		{
			FVector AnchorWorld = FVector::ZeroVector;
			bool bHaveWorld = false;
			{
				FComposableCameraShot EffectiveShot;
				if (BuildEffectiveShotForPreview(EffectiveShot))
				{
					const FComposableCameraAnchorSpec& Anchor =
						AnchorAccessor(EffectiveShot);
					bHaveWorld = Anchor.ResolveWorldPosition(
						EffectiveShot.Targets, AnchorWorld);
				}
			}

			TArray<FString, TInlineAllocator<4>> Lines;
			Lines.Add(FString::Printf(TEXT("%s anchor"), LabelText));
			if (HandleType == EHandleType::PlacementAnchor)
			{
				Lines.Add(FString::Printf(
					TEXT("Distance: %.1f cm"),
					ActiveShot->Placement.Distance));
			}
			Lines.Add(FString::Printf(
				TEXT("Screen: (%.3f, %.3f)"),
				AuthoredScreenPos.X, AuthoredScreenPos.Y));
			Lines.Add(bHaveWorld
				? FString::Printf(TEXT("World: (%.0f, %.0f, %.0f)"),
					AnchorWorld.X, AnchorWorld.Y, AnchorWorld.Z)
				: FString(TEXT("World: unresolved")));

			float TipY = PixelPos.Y + Radius + 6.f;
			const float TipX = PixelPos.X + Radius + 6.f;
			for (const FString& Line : Lines)
			{
				FCanvasTextItem Item(
					FVector2D(TipX, TipY),
					FText::FromString(Line),
					HandleLabelFont, Fill);
				Item.EnableShadow(FLinearColor::Black);
				Canvas.DrawItem(Item);
				TipY += 14.f;
			}
		}

		if (bHandleInteractive)
		{
			CachedHandles.Add({ PixelPos, HandleType });
		}
	};

	// Placement anchor handle (yellow). Greyed out + non-interactive when
	// OrbitMode == ByDirection (ScreenPosition unused).
	DrawAnchorHandle(
		EHandleType::PlacementAnchor,
		ActiveShot->Placement.ScreenPosition,
		[](const FComposableCameraShot& S) -> const FComposableCameraAnchorSpec&
		{ return S.Placement.PlacementAnchor; },
		kHandlePlacementColor,
		TEXT("Placement"),
		bPlacementDisabled,
		bPlacementInteractive);

	// Aim anchor handle (cyan). Greyed out + non-interactive when AimMode == NoOp.
	DrawAnchorHandle(
		EHandleType::AimAnchor,
		ActiveShot->Aim.ScreenPosition,
		[](const FComposableCameraShot& S) -> const FComposableCameraAnchorSpec&
		{ return S.Aim.AimAnchor; },
		kHandleAimColor,
		TEXT("Aim"),
		bAimDisabled,
		bAimInteractive);
}

bool FComposableCameraShotEditorViewportClient::HitTestHandles(
	int32 PixelX, int32 PixelY,
	EHandleType& OutType) const
{
	const float HitRadiusSq = kHandleHitRadius * kHandleHitRadius;

	// Iterate in reverse so handles drawn last (z-top in our DrawHandles
	// order) win the hit test on overlap.
	for (int32 i = CachedHandles.Num() - 1; i >= 0; --i)
	{
		const FHandleScreenPosCache& H = CachedHandles[i];
		const float DX = static_cast<float>(PixelX) - H.PixelPos.X;
		const float DY = static_cast<float>(PixelY) - H.PixelPos.Y;
		if (DX * DX + DY * DY <= HitRadiusSq)
		{
			OutType = H.Type;
			return true;
		}
	}
	OutType = EHandleType::None;
	return false;
}

void FComposableCameraShotEditorViewportClient::ApplyDragToShot(int32 PixelX, int32 PixelY)
{
	if (!ActiveShot || !Viewport || ActiveDragHandleType == EHandleType::None)
	{
		return;
	}

	const FIntPoint VPSize = Viewport->GetSizeXY();
	FVector2D NewScreenPos = PixelToNormalizedScreen(PixelX, PixelY, VPSize);
	NewScreenPos.X = FMath::Clamp(NewScreenPos.X, -0.5f, 0.5f);
	NewScreenPos.Y = FMath::Clamp(NewScreenPos.Y, -0.5f, 0.5f);

	if (ActiveDragHandleType == EHandleType::PlacementAnchor)
	{
		ActiveShot->Placement.ScreenPosition = NewScreenPos;
	}
	else if (ActiveDragHandleType == EHandleType::AimAnchor)
	{
		ActiveShot->Aim.ScreenPosition = NewScreenPos;
	}

	// Skip per-frame PostEditChangeProperty(Interactive). For a section host,
	// firing this every mouse move triggers Sequencer's section-edit listener
	// chain, which transiently resets the source SK's bone array — that
	// surfaces as a one-frame A-pose flash on the preview proxy each frame
	// of the drag. The solver reads `ActiveShot` directly each tick, so the
	// live drag visualization is unaffected. The final ValueSet notification
	// fires once on EndDrag, which is enough for Details-panel refresh and
	// other commit-time listeners.
}

void FComposableCameraShotEditorViewportClient::EndDrag()
{
	if (ActiveDragHandleType == EHandleType::None)
	{
		return;
	}

	// Final commit — ValueSet change type so host listeners that distinguish
	// "live drag" from "settled value" (e.g. expensive caches) update only
	// at the end.
	if (UObject* Host = ActiveHost.Get())
	{
		if (FProperty* ShotProp = Host->GetClass()->FindPropertyByName(TEXT("Shot")))
		{
			FPropertyChangedEvent Event(ShotProp, EPropertyChangeType::ValueSet);
			Host->PostEditChangeProperty(Event);
		}
	}

	// Drop the transaction — destructor closes it; undo system records the
	// whole drag as a single step from start (Modify call) to here.
	DragTransaction.Reset();

	ActiveDragHandleType = EHandleType::None;
}

void FComposableCameraShotEditorViewportClient::StartRollDrag()
{
	if (!ActiveShot || !Viewport || bRollDragActive
		|| ActiveDragHandleType != EHandleType::None)
	{
		return;   // can't start — no Shot, or already mid-gesture
	}

	RollTransaction = MakeUnique<FScopedTransaction>(
		LOCTEXT("DragShotRoll", "Drag Shot Roll"));
	if (UObject* Host = ActiveHost.Get())
	{
		// Same SaveToTransactionBuffer-bypass-Modify pattern as the LMB
		// handle drag (see InputKey LMB branch + TechDoc §7.2). For a
		// Sequencer Section host, `Modify()` broadcasts OnObjectModified
		// → Sequencer invalidates eval cache → Spawnable re-spawn →
		// one-frame A-pose flash on every mid-drag write. The bypass
		// records the undo snapshot only; EndRollDrag's
		// PostEditChangeProperty(ValueSet) is the single broadcast.
		SaveToTransactionBuffer(Host, /*bMarkDirty=*/false);
	}
	bRollDragActive    = true;
	RollDragLastMouseX = Viewport->GetMouseX();
}

void FComposableCameraShotEditorViewportClient::ApplyRollDrag(int32 PixelX)
{
	if (!bRollDragActive || !ActiveShot)
	{
		return;
	}

	// 0.5°/pixel matches the perceived "feel" of base RMB-look's yaw
	// rate on a default 1920-wide viewport — quarter-width drag (~480 px)
	// gives ~240° of Roll, full-width drag wraps. Per-pixel rate feels
	// right for both fine adjustments and large rolls.
	constexpr float RollDegPerPixel = 0.5f;

	const int32 DeltaX = PixelX - RollDragLastMouseX;
	if (DeltaX != 0)
	{
		ActiveShot->Roll = FMath::UnwindDegrees(
			ActiveShot->Roll + DeltaX * RollDegPerPixel);
		// No PostEditChangeProperty(Interactive) per frame — same
		// rationale as ApplyDragToShot. Drag mode picks up Shot.Roll
		// via the per-tick solver; Free mode picks it up via the
		// view.Roll = Shot.Roll sync at the end of
		// RunSolverAndDriveCamera. Both produce immediate visual
		// feedback within the same frame the cursor moves.
	}
	RollDragLastMouseX = PixelX;
}

void FComposableCameraShotEditorViewportClient::EndRollDrag()
{
	if (!bRollDragActive)
	{
		return;
	}

	if (UObject* Host = ActiveHost.Get())
	{
		if (FProperty* ShotProp = Host->GetClass()->FindPropertyByName(TEXT("Shot")))
		{
			FPropertyChangedEvent Event(ShotProp, EPropertyChangeType::ValueSet);
			Host->PostEditChangeProperty(Event);
		}
	}

	RollTransaction.Reset();
	bRollDragActive = false;
}

bool FComposableCameraShotEditorViewportClient::TryAdjustDistanceFromMouseWheel(bool bScrollUp)
{
	if (!ActiveShot)
	{
		return false;
	}

	// Only AnchorOrbit / AnchorAtScreen consume Distance in the forward
	// solver. FixedWorldPosition reads `Placement.FixedWorldPosition`
	// directly and ignores Distance entirely — bumping it would silently
	// edit a hidden field with no visible effect, so refuse the wheel
	// instead.
	const EShotPlacementMode Mode = ActiveShot->Placement.Mode;
	if (Mode != EShotPlacementMode::AnchorOrbit
		&& Mode != EShotPlacementMode::AnchorAtScreen)
	{
		return false;
	}

	// Multiplicative zoom — feels uniform across distance ranges (a
	// click at 10 m moves by ~1 m; a click at 1 m moves by ~10 cm).
	// Scroll up = zoom in (distance shrinks); scroll down = zoom out.
	// Default 1.1× per click matches PIE / Persona / standard editor
	// viewport wheel dolly. Modifier keys scale the per-click *step*
	// (≡ factor − 1) to match DCC convention so a single rate doesn't
	// feel coarse-or-tedious depending on geometry scale: Shift = 5×
	// step (factor 1.5, ~50% per click) for fast traversal; Ctrl = 0.2×
	// step (factor 1.02, ~2% per click) for fine framing. Holding both
	// (or neither) falls back to the default — composing the two is
	// ambiguous (1× of default ≡ default), and refusing to disambiguate
	// is safer than picking arbitrarily.
	const bool bShiftHeld = Viewport
		&& (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift));
	const bool bCtrlHeld = Viewport
		&& (Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl));
	const float ZoomFactor =
		ComposableCameraSystem::ShotEditorWheelMath::ComputeWheelZoomFactor(bShiftHeld, bCtrlHeld);

	const float OldDistance = ActiveShot->Placement.Distance;
	const float NewDistance = bScrollUp
		? OldDistance / ZoomFactor
		: OldDistance * ZoomFactor;
	// Clamp to the canonical authoring range. Lower bound matches the
	// solver's pre-flight floor (1cm); upper bound is the 100m soft cap
	// against scroll-spam (Shift+wheel reaches 1e9 in ~10 clicks
	// otherwise). UPROPERTY meta clamps the Details-panel slider to the
	// same range, but meta doesn't enforce on direct field writes —
	// gesture writers must opt in explicitly.
	const float ClampedDistance = FMath::Clamp(
		NewDistance, FShotPlacement::MinDistance, FShotPlacement::MaxDistance);

	if (FMath::IsNearlyEqual(ClampedDistance, OldDistance))
	{
		// Already at the floor and trying to shrink further — no-op,
		// don't bother with the transaction or notification.
		return false;
	}

	UObject* Host = ActiveHost.Get();
	{
		// Per-click atomic transaction. Unlike the handle drag (which
		// uses `SaveToTransactionBuffer` + deferred `PostEditChangeProperty`
		// to avoid Sequencer's mid-gesture re-spawn flash), wheel events
		// are atomic — one click = one commit — so the standard
		// Modify + ValueSet pattern is appropriate; the flash, if any,
		// happens once per click and immediately settles, matching what
		// dragging the Distance slider in the Details panel produces.
		FScopedTransaction Tx(LOCTEXT("WheelAdjustShotDistance", "Adjust Shot Distance"));
		if (Host)
		{
			Host->Modify();
		}
		ActiveShot->Placement.Distance = ClampedDistance;
		if (Host)
		{
			if (FProperty* ShotProp = Host->GetClass()->FindPropertyByName(TEXT("Shot")))
			{
				FPropertyChangedEvent Event(ShotProp, EPropertyChangeType::ValueSet);
				Host->PostEditChangeProperty(Event);
			}
		}
	}
	return true;
}

bool FComposableCameraShotEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// Lock mode: eat all MOUSE input (no handle drag, no camera control,
	// no scroll-zoom) but let KEYBOARD events fall through to the base
	// class. Reasoning: keyboard events that reach the viewport client
	// only do so when the viewport widget is focused; the Details panel's
	// SEditableTextBox / numeric input widgets handle their own keyboard
	// via OnKeyChar/OnKeyDown and never reach us. But editor-wide
	// shortcuts (Ctrl+Z undo, Ctrl+S save, etc.) DO route through
	// FEditorViewportClient::InputKey when the viewport is focused, and
	// blocking those during Lock would silently break workflows that
	// involve Lock-then-Ctrl+Z. Mouse-only eat keeps Lock honest about
	// "no viewport interaction" without surprising keyboard side-effects.
	if (CurrentMode == EShotEditorMode::Lock)
	{
		if (EventArgs.Key.IsMouseButton())
		{
			return true;
		}
		return FEditorViewportClient::InputKey(EventArgs);
	}

	// Helper: is Alt currently held? Used by the Alt+RMB Roll path.
	const auto IsAltHeld = [&]() -> bool
	{
		return Viewport
			&& (Viewport->KeyState(EKeys::LeftAlt)
				|| Viewport->KeyState(EKeys::RightAlt));
	};

	// Alt+RMB Roll drag — supported in BOTH Drag and Free modes (Lock
	// eats all mouse input below). Detected at the top so it preempts
	// the mode-specific mouse-handling branches below: in Drag mode it
	// runs before the catch-all "eat all mouse buttons" guard; in Free
	// mode it runs before the fall-through to base-class RMB-look. The
	// gesture writes `Shot.Roll` (NOT view-rotation Roll) inside a
	// transaction so Ctrl+Z restores the prior value — Drag mode picks
	// up the change via the next-tick solver, Free mode via the
	// view.Roll = Shot.Roll sync at the bottom of
	// RunSolverAndDriveCamera. Returning `true` on the press makes
	// Slate capture the mouse for us (same path the LMB handle drag
	// uses), so CapturedMouseMove fires for the gesture's motion
	// regardless of which mode we're in.
	if (CurrentMode != EShotEditorMode::Lock
		&& EventArgs.Key == EKeys::RightMouseButton
		&& Viewport)
	{
		if (EventArgs.Event == IE_Pressed && IsAltHeld() && !bRollDragActive)
		{
			StartRollDrag();
			return true;
		}
		if (EventArgs.Event == IE_Released && bRollDragActive)
		{
			EndRollDrag();
			return true;
		}
	}

	// Drag mode: solver drives the camera, designer interacts only through
	// the on-screen anchor handles. LMB-on-handle starts a drag; every
	// other mouse input is **eaten** so the base class can't engage its
	// orbit / pan / dolly camera-track behavior. Free camera movement
	// is the Free mode's job — Drag stays solver-authoritative.
	if (CurrentMode == EShotEditorMode::Drag)
	{
		if (EventArgs.Key == EKeys::LeftMouseButton && Viewport)
		{
			if (EventArgs.Event == IE_Pressed && ActiveShot)
			{
				EHandleType HitType;
				if (HitTestHandles(Viewport->GetMouseX(), Viewport->GetMouseY(), HitType))
				{
					// Start handle drag — begin a transaction so the whole
					// gesture undoes as one entry, snapshot host state for undo.
					DragTransaction = MakeUnique<FScopedTransaction>(
						LOCTEXT("DragShotScreenPosition", "Drag Shot Screen Position"));
					if (UObject* Host = ActiveHost.Get())
					{
						// CRITICAL: bypass UObject::Modify and call
						// SaveToTransactionBuffer directly. Modify(true) calls
						// UMovieSceneSignedObject::MarkAsChanged →
						// OnSignatureChangedEvent broadcast → Sequencer
						// invalidates evaluation cache → Spawnable re-spawn →
						// fresh actor at ref pose for one tick → SyncProxyTransforms
						// reads that ref pose → preview A-pose flash. Even
						// Modify(false) broadcasts FCoreUObjectDelegates::OnObjectModified
						// (Obj.cpp line 1544, unconditional regardless of
						// bAlwaysMarkDirty), and Sequencer also reacts to that.
						// SaveToTransactionBuffer is the bare-bones path —
						// records undo snapshot only, no broadcasts. EndDrag's
						// PostEditChangeProperty(ValueSet) signals everything
						// in one shot at commit, which is the right time.
						SaveToTransactionBuffer(Host, /*bMarkDirty=*/false);
					}
					ActiveDragHandleType  = HitType;
					return true;
				}
				// LMB pressed off-handle in Drag mode → eat so base class
				// doesn't start a camera-track gesture.
				return true;
			}
			if (EventArgs.Event == IE_Released)
			{
				if (ActiveDragHandleType != EHandleType::None)
				{
					EndDrag();
				}
				return true;   // eat release regardless of whether a drag was active
			}
		}

		// Mouse wheel → modify `Shot.Placement.Distance` for the modes
		// that read it (AnchorOrbit / AnchorAtScreen). FixedWorldPosition
		// ignores the wheel (helper returns false) and the event drops
		// through to the general mouse-button eater below — so the wheel
		// is silent in that mode rather than being a base-class dolly
		// (which would be confusing in Drag mode where the camera is
		// solver-driven). Note: `MouseScrollUp` / `MouseScrollDown` come
		// in as IE_Pressed events (no IE_Released for wheel ticks);
		// they're FKey instances that report `IsMouseButton() == true`,
		// so the catch-all below would eat them otherwise — handle here
		// first.
		if ((EventArgs.Key == EKeys::MouseScrollUp || EventArgs.Key == EKeys::MouseScrollDown)
			&& EventArgs.Event == IE_Pressed)
		{
			if (TryAdjustDistanceFromMouseWheel(EventArgs.Key == EKeys::MouseScrollUp))
			{
				return true;
			}
			// Fall through — FixedWorldPosition mode returns false; the
			// catch-all below still consumes the wheel so base class
			// doesn't dolly the camera (preserving the "Drag mode is
			// solver-authoritative" invariant from §23.x).
		}

		// Any other mouse button (RMB / MMB / unhandled wheel) → eat so
		// base class can't engage RMB-look / MMB-pan / wheel-zoom.
		// Keyboard falls through so editor-wide shortcuts (Ctrl+Z etc.)
		// still work.
		if (EventArgs.Key.IsMouseButton())
		{
			return true;
		}
		return FEditorViewportClient::InputKey(EventArgs);
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

void FComposableCameraShotEditorViewportClient::CapturedMouseMove(
	FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	if (ActiveDragHandleType != EHandleType::None)
	{
		ApplyDragToShot(InMouseX, InMouseY);
		return;   // don't forward — base would interpret as camera-track drag
	}
	if (bRollDragActive)
	{
		ApplyRollDrag(InMouseX);
		return;   // don't forward — base would yaw the camera
	}
	FEditorViewportClient::CapturedMouseMove(InViewport, InMouseX, InMouseY);
}

void FComposableCameraShotEditorViewportClient::MouseMove(
	FViewport* InViewport, int32 X, int32 Y)
{
	FEditorViewportClient::MouseMove(InViewport, X, Y);

	// Hover detection — only meaningful in Drag mode (handles are
	// non-interactive in Free / Lock).
	if (CurrentMode != EShotEditorMode::Drag
		|| ActiveDragHandleType != EHandleType::None)
	{
		HoveredHandleType = EHandleType::None;
		return;
	}
	EHandleType HitType;
	HoveredHandleType = HitTestHandles(X, Y, HitType) ? HitType : EHandleType::None;
}

// (Alt+RMB Roll handling lives in InputKey + CapturedMouseMove, NOT
// InputAxis. Earlier prototype routed through InputAxis to preempt base
// RMB-look's yaw/pitch — but that approach only worked in Free mode
// (where base RMB-look engages capture); Drag mode eats RMB at InputKey
// before base ever sees it, so capture never starts and InputAxis never
// fires. Switching to InputKey-consumes-RMB-press lets Slate capture for
// us regardless of mode and routes mouse motion through CapturedMouseMove,
// which works uniformly across Drag and Free.)


// ─── 4.3 Reverse solve (Free → Drag dialog) ──────────────────────────────

EShotEditorReverseSolveStatus FComposableCameraShotEditorViewportClient::DiagnoseReverseSolveCurrentCamera() const
{
	if (!ActiveShot)
	{
		return EShotEditorReverseSolveStatus::NoActiveShot;
	}
	// Apply override resolution so reverse-solve availability tracks the
	// effective (override-resolved) shot, not the raw placeholder. A Section
	// with `Targets[0].Actor=None` + a binding override should still be
	// reverse-solvable.
	FComposableCameraShot EffectiveShot;
	if (!BuildEffectiveShotForPreview(EffectiveShot))
	{
		return EShotEditorReverseSolveStatus::EffectiveShotInvalid;
	}
	FVector PlacementAnchorWorld;
	if (!EffectiveShot.Placement.PlacementAnchor.ResolveWorldPosition(
			EffectiveShot.Targets, PlacementAnchorWorld))
	{
		return EShotEditorReverseSolveStatus::PlacementAnchorUnresolvable;
	}
	FVector AimAnchorWorld;
	if (!EffectiveShot.Aim.AimAnchor.ResolveWorldPosition(
			EffectiveShot.Targets, AimAnchorWorld))
	{
		return EShotEditorReverseSolveStatus::AimAnchorUnresolvable;
	}

	// AnchorAtScreen-only: the joint solve writes Distance as cam-frame
	// depth (X coord of PlacementAnchor under user's free-flown rotation).
	// When PlacementAnchor is at or behind the camera, that depth is ≤ 0
	// and the reverse path can't recover a valid Distance — the actual
	// runtime check lives in ReverseSolveCurrentCameraToShot but mirroring
	// it here surfaces the failure in the dialog body before the user
	// clicks Save (rather than silently no-oping after the click).
	if (EffectiveShot.Placement.Mode == EShotPlacementMode::AnchorAtScreen)
	{
		const FVector  CamPos = GetViewLocation();
		const FRotator CamRot = GetViewRotation();
		const FVector  PCam   = CamRot.UnrotateVector(PlacementAnchorWorld - CamPos);
		if (PCam.X <= UE_KINDA_SMALL_NUMBER)
		{
			return EShotEditorReverseSolveStatus::PlacementAnchorBehindCamera;
		}
	}

	return EShotEditorReverseSolveStatus::Ok;
}

bool FComposableCameraShotEditorViewportClient::CanReverseSolveCurrentCamera() const
{
	return DiagnoseReverseSolveCurrentCamera() == EShotEditorReverseSolveStatus::Ok;
}

FText ShotEditorReverseSolveStatusToText(EShotEditorReverseSolveStatus Status)
{
	switch (Status)
	{
	case EShotEditorReverseSolveStatus::Ok:
		return FText::GetEmpty();
	case EShotEditorReverseSolveStatus::NoActiveShot:
		return LOCTEXT("ReverseSolveStatus_NoActiveShot",
			"No Shot is bound to the editor.");
	case EShotEditorReverseSolveStatus::EffectiveShotInvalid:
		return LOCTEXT("ReverseSolveStatus_EffectiveShotInvalid",
			"Shot data could not be resolved (binding overrides may be incomplete).");
	case EShotEditorReverseSolveStatus::PlacementAnchorUnresolvable:
		return LOCTEXT("ReverseSolveStatus_PlacementAnchorUnresolvable",
			"Placement anchor is unresolvable — assign a valid Target to the Placement layer.");
	case EShotEditorReverseSolveStatus::AimAnchorUnresolvable:
		return LOCTEXT("ReverseSolveStatus_AimAnchorUnresolvable",
			"Aim anchor is unresolvable — assign a valid Target to the Aim layer.");
	case EShotEditorReverseSolveStatus::PlacementAnchorBehindCamera:
		return LOCTEXT("ReverseSolveStatus_PlacementAnchorBehindCamera",
			"Placement anchor is at or behind the camera — move the camera so the anchor is in front of it.");
	}
	return FText::GetEmpty();
}

bool FComposableCameraShotEditorViewportClient::ReverseSolveCurrentCameraToShot()
{
	using namespace ComposableCameraSystem::ShotSolver;

	if (!Viewport)
	{
		return false;
	}

	// Single pre-flight via Diagnose so the failure log line names the
	// specific reason instead of the generic "returned false". Mirrors what
	// the dialog body shows the designer — useful in cases where the user
	// bypasses the dialog (future Save-current-Shot toolbar action) and
	// relies on the log to understand why nothing happened.
	const EShotEditorReverseSolveStatus Status = DiagnoseReverseSolveCurrentCamera();
	if (Status != EShotEditorReverseSolveStatus::Ok)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("ShotEditor: reverse-solve aborted — %s"),
			*ShotEditorReverseSolveStatusToText(Status).ToString());
		return false;
	}

	// Diagnose already validated these; re-resolve for the write path.
	// (Keeping the recompute here rather than threading state out of
	// Diagnose — the cost is one extra ResolveWorldPosition per commit,
	// which is dwarfed by the FScopedTransaction below.)
	FComposableCameraShot EffectiveShot;
	BuildEffectiveShotForPreview(EffectiveShot);
	FVector PlacementAnchorWorld;
	FVector AimAnchorWorld;
	EffectiveShot.Placement.PlacementAnchor.ResolveWorldPosition(
		EffectiveShot.Targets, PlacementAnchorWorld);
	EffectiveShot.Aim.AimAnchor.ResolveWorldPosition(
		EffectiveShot.Targets, AimAnchorWorld);

	const FVector  CamPos = GetViewLocation();
	const FRotator CamRot = GetViewRotation();
	const float    FOV    = ViewFOV;
	const FIntPoint VPSize = Viewport->GetSizeXY();
	const float    Aspect = (VPSize.Y > 0)
		? static_cast<float>(VPSize.X) / static_cast<float>(VPSize.Y)
		: 16.f / 9.f;
	const float    TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
	const float    TanHalfVOR = TanHalfHOR / Aspect;

	// Per-mode reverse-solve. Each Placement mode reads a disjoint subset of
	// fields in the forward solver (see DataAssets/ComposableCameraShot.h
	// EShotPlacementMode docs); the reverse must write the same subset, so a
	// freely-flown camera in Free mode round-trips back to the same pose
	// when forward-solved with the committed values.
	const EShotPlacementMode PlacementMode = EffectiveShot.Placement.Mode;

	float     NewDistance         = ActiveShot->Placement.Distance;
	FVector2D NewLocalCameraDir   = ActiveShot->Placement.LocalCameraDirection;
	FVector2D NewPlacementScreen  = ActiveShot->Placement.ScreenPosition;
	FVector   NewFixedWorldPos    = ActiveShot->Placement.FixedWorldPosition;

	switch (PlacementMode)
	{
	case EShotPlacementMode::AnchorOrbit:
		{
			// Pure spherical placement: Distance is Euclidean,
			// LocalCameraDirection is the (Yaw, Pitch) of the cam-from-anchor
			// unit vector in basis-frame coords. Placement.ScreenPosition is
			// unread by the forward solver in this mode (see
			// SolvePlacement → SolveAnchorOrbitPosition with ScreenPos forced
			// to ZeroVector), so leave it untouched.
			const FVector AnchorToCam = CamPos - PlacementAnchorWorld;
			NewDistance = FMath::Clamp(
				static_cast<float>(AnchorToCam.Length()),
				FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
			const FVector DirWorld = (NewDistance > UE_KINDA_SMALL_NUMBER)
				? AnchorToCam / NewDistance
				: FVector(1.f, 0.f, 0.f);

			// Resolve basis from EffectiveShot so a Sequencer-bound override
			// actor's quat is honored (matches the basis the solver would use
			// in RunSolverAndDriveCamera, which also goes through the
			// effective shot).
			const FQuat   Basis    = ResolvePlacementBasis(EffectiveShot);
			const FVector DirLocal = Basis.Inverse().RotateVector(DirWorld);

			NewLocalCameraDir.X = FMath::RadiansToDegrees(
				FMath::Atan2(DirLocal.Y, DirLocal.X));
			NewLocalCameraDir.Y = FMath::RadiansToDegrees(
				FMath::Asin(FMath::Clamp(DirLocal.Z, -1.f, 1.f)));
			break;
		}

	case EShotPlacementMode::AnchorAtScreen:
		{
			// Joint-solve placement: Distance is cam-frame depth (X coord of
			// PlacementAnchor under the joint-solve camera rotation), and
			// Placement.ScreenPosition is PlacementAnchor's projected screen
			// coords. Round-trip rationale: the forward solver pre-rotates
			// authored Placement.ScreenPosition by -Roll (anisotropic), runs
			// the iteration in the Roll=0 frame, then composes Roll onto the
			// output rotation. Projecting PlacementAnchor through the user's
			// FULL rotation here recovers exactly the post-Roll authored
			// value the solver will reproduce.
			//
			// LocalCameraDirection / FixedWorldPosition are unread in this
			// mode; leave their fields untouched so toggling back to
			// AnchorOrbit later doesn't lose the prior orbital authoring.
			// Pre-flight already gated PCam.X > 0 via
			// DiagnoseReverseSolveCurrentCamera (PlacementAnchorBehindCamera
			// status). No defensive recheck here — divergence between
			// Diagnose and this branch would only happen across an inter-
			// frame view-rotation change, which the Free→Drag dialog
			// doesn't permit (modal blocks input).
			const FVector PCam = CamRot.UnrotateVector(PlacementAnchorWorld - CamPos);
			NewDistance = FMath::Clamp(
				static_cast<float>(PCam.X),
				FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
			NewPlacementScreen.X = FMath::Clamp(
				static_cast<float>(PCam.Y / (2.f * TanHalfHOR * PCam.X)), -0.5f, 0.5f);
			NewPlacementScreen.Y = FMath::Clamp(
				static_cast<float>(PCam.Z / (2.f * TanHalfVOR * PCam.X)), -0.5f, 0.5f);
			break;
		}

	case EShotPlacementMode::FixedWorldPosition:
		{
			// Camera lives at an explicit world point — Distance / direction
			// / screen-pos are all unread in forward solve. Capture the user's
			// freely-flown world position; rotation comes from the Aim layer
			// path below.
			NewFixedWorldPos = CamPos;
			break;
		}
	}

	// Aim → Aim.ScreenPosition. Only LookAtAnchor reads this field; NoOp
	// short-circuits before AimAnchor resolution and ignores ScreenPosition,
	// so don't overwrite the authored value when the mode isn't using it.
	FVector2D NewAimScreen = ActiveShot->Aim.ScreenPosition;
	if (EffectiveShot.Aim.Mode == EShotAimMode::LookAtAnchor)
	{
		FVector2D Projected;
		if (ComposableCameraSystem::ProjectWorldPointToScreen(
			AimAnchorWorld, CamPos, CamRot, TanHalfHOR, Aspect, Projected))
		{
			NewAimScreen.X = FMath::Clamp(Projected.X, -0.5f, 0.5f);
			NewAimScreen.Y = FMath::Clamp(Projected.Y, -0.5f, 0.5f);
		}
	}

	// Commit — wrapped in a transaction so undo reverts the whole
	// reverse-solve as one entry.
	{
		FScopedTransaction ReverseSolveTransaction(
			LOCTEXT("ReverseSolveCamera", "Save Camera Framing as Shot Params"));

		UObject* Host = ActiveHost.Get();
		if (Host)
		{
			Host->Modify();
		}

		// Mode-scoped writes — only fields the forward solver reads in this
		// Placement mode are committed. Other Placement fields keep their
		// prior authored values so mode-toggling preserves the alternate
		// authoring (e.g. designer flipping AnchorOrbit ↔ AnchorAtScreen
		// retains the orbital direction across the round-trip).
		switch (PlacementMode)
		{
		case EShotPlacementMode::AnchorOrbit:
			ActiveShot->Placement.Distance             = NewDistance;
			ActiveShot->Placement.LocalCameraDirection = NewLocalCameraDir;
			break;
		case EShotPlacementMode::AnchorAtScreen:
			ActiveShot->Placement.Distance       = NewDistance;
			ActiveShot->Placement.ScreenPosition = NewPlacementScreen;
			break;
		case EShotPlacementMode::FixedWorldPosition:
			ActiveShot->Placement.FixedWorldPosition = NewFixedWorldPos;
			break;
		}

		ActiveShot->Aim.ScreenPosition = NewAimScreen;
		// Capture user-authored Roll from the freely-flown camera so the
		// forward solver re-applies it on next tick. Roll is consumed by
		// every (Placement, Aim) mode pair, so always commit it.
		// `FMath::UnwindDegrees` normalizes to [-180, 180] — matches the
		// `Shot.Roll` UPROPERTY clamp meta, so reverse-solve doesn't
		// commit a value the Details panel would later truncate (FRotator
		// stores raw values; arbitrary view-roll on entry to Free mode
		// could land outside that range).
		ActiveShot->Roll = FMath::UnwindDegrees(CamRot.Roll);

		if (ActiveShot->Lens.FOVMode == EShotFOVMode::Manual)
		{
			ActiveShot->Lens.ManualFOV = FOV;
		}

		// Final notify so Details panel refreshes + listeners react.
		if (Host)
		{
			if (FProperty* ShotProp = Host->GetClass()->FindPropertyByName(TEXT("Shot")))
			{
				FPropertyChangedEvent Event(ShotProp, EPropertyChangeType::ValueSet);
				Host->PostEditChangeProperty(Event);
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

void FComposableCameraShotEditorViewportClient::RebuildProxies()
{
	DestroyProxies();
	LastRebuiltHost = ActiveHost;

	if (!ActiveShot || !PreviewScene)
	{
		return;
	}

	ProxyActors.Reserve(ActiveShot->Targets.Num());
	LastResolvedSources.Reset(ActiveShot->Targets.Num());
	for (int32 i = 0; i < ActiveShot->Targets.Num(); ++i)
	{
		AActor* Source = ResolveSourceActorForTargetIndex(i);
		AActor* Proxy = SpawnProxyForActor(Source);
		ProxyActors.Add(Proxy);
		LastResolvedSources.Add(Source);
	}
}

void FComposableCameraShotEditorViewportClient::DestroyProxies()
{
	for (TWeakObjectPtr<AActor>& WeakProxy : ProxyActors)
	{
		if (AActor* Proxy = WeakProxy.Get())
		{
			Proxy->Destroy();
		}
	}
	ProxyActors.Reset();
	LastResolvedSources.Reset();
}

void FComposableCameraShotEditorViewportClient::SyncProxyTransforms()
{
	if (!ActiveShot)
	{
		return;
	}

	const int32 N = FMath::Min(ProxyActors.Num(), ActiveShot->Targets.Num());
	for (int32 i = 0; i < N; ++i)
	{
		AActor* Proxy = ProxyActors[i].Get();
		if (!Proxy)
		{
			continue;
		}
		// Use the same override-aware resolver as RebuildProxies so a
		// Section's TargetActorOverrides actually drive per-frame transform
		// sync. The previous direct read of `Targets[i].Target.Actor.Get()`
		// hit the placeholder (which is None for binding-driven sections)
		// and parked the proxy at z=-100000, making the preview useless.
		AActor* Source = ResolveSourceActorForTargetIndex(i);
		if (!Source)
		{
			// Source went away — hide the proxy at infinity rather than
			// destroying it (rebuild on next iteration handles count drift).
			Proxy->SetActorLocation(FVector(0.f, 0.f, -100000.f));
			continue;
		}

		// Source-mesh-component-aware transform sync — same rationale as in
		// SpawnProxyForActor: ACharacter offsets its Mesh component by
		// (0, 0, -88) within the actor, so source mesh world transform !=
		// source actor world transform. ASkeletalMeshActor / AStaticMeshActor
		// proxies have mesh-as-root, so syncing proxy actor transform to
		// SOURCE MESH world transform keeps the proxy mesh aligned with the
		// source mesh on every frame (without this, ACharacter sources end
		// up 88cm higher in the preview than they are in the level).
		FTransform SrcTransform;
		if (USkeletalMeshComponent* SrcSK = Source->FindComponentByClass<USkeletalMeshComponent>())
		{
			SrcTransform = SrcSK->GetComponentTransform();

			// Direct component-space pose copy: read source's CST array
			// (post-anim-eval bone transforms in component space), write
			// onto proxy's editable CST, flip the double buffer. Skips
			// both LeaderPose and AnimBP entirely on the proxy side, so
			// Sequencer's transient bone-array resets on the source can't
			// surface as A-pose flashes on the proxy — the next frame's
			// SyncProxyTransforms reads whatever the source has settled
			// to AFTER Sequencer finishes its evaluation pass. Requires
			// matching skeleton (same SkeletalMeshAsset on both sides);
			// SpawnProxyForActor sets ProxySK->SetSkeletalMeshAsset(Mesh)
			// from SrcSK's mesh asset so this holds.
			if (USkeletalMeshComponent* ProxySK = Cast<USkeletalMeshComponent>(Proxy->GetRootComponent()))
			{
				const TArray<FTransform>& SrcCST = SrcSK->GetComponentSpaceTransforms();
				TArray<FTransform>& ProxyCST = ProxySK->GetEditableComponentSpaceTransforms();
				if (SrcCST.Num() > 0 && SrcCST.Num() == ProxyCST.Num())
				{
					ProxyCST = SrcCST;
					ProxySK->ApplyEditedComponentSpaceTransforms();
				}
			}
		}
		else if (UStaticMeshComponent* SrcSM = Source->FindComponentByClass<UStaticMeshComponent>())
		{
			SrcTransform = SrcSM->GetComponentTransform();
		}
		else
		{
			// No mesh component (cylinder fallback case) — use actor transform
			// directly since the proxy was authored against actor location.
			SrcTransform = Source->GetActorTransform();
		}

		// Proxy's scale is preserved on a per-proxy basis (the capsule
		// fallback sets a non-unit scale; SK / SM proxies stay at unit).
		// So we copy location + rotation only.
		Proxy->SetActorLocationAndRotation(
			SrcTransform.GetLocation(), SrcTransform.GetRotation());
	}
}

void FComposableCameraShotEditorViewportClient::RunSolverAndDriveCamera()
{
	using namespace ComposableCameraSystem::ShotSolver;

	if (!ActiveShot || !Viewport)
	{
		return;
	}

	FShotSolveContext Context;
	const FIntPoint VPSize = Viewport->GetSizeXY();
	Context.ViewportAspectRatio = (VPSize.Y > 0)
		? static_cast<float>(VPSize.X) / static_cast<float>(VPSize.Y)
		: 16.f / 9.f;
	// Use the current ViewFOV as the previous-frame FOV — the solver's
	// SolvedFromBoundsFit mode converges in 1-2 frames; Manual mode ignores it.
	Context.PreviousFrameFOV = ViewFOV;

	// Apply per-section TargetActorOverrides before solving so the camera
	// follows the bound LS actor instead of the Shot's placeholder. Without
	// this, an Inline shot whose Targets[i].Actor is None would always
	// produce an UNRESOLVED solve in the preview viewport even though the
	// runtime evaluation (LSComponent's TrackInstance push) does see the
	// override-resolved actors.
	FComposableCameraShot EffectiveShot;
	if (!BuildEffectiveShotForPreview(EffectiveShot))
	{
		return;
	}

	// Seed AutoFromComponentBounds caches on the EffectiveShot copy. Editor
	// preview can't honor `EBoundsCachePolicy::StaticSnapshot` cleanly: the
	// runtime node refreshes once in `OnInitialize` against its own Shot
	// instance, but here EffectiveShot is rebuilt every tick from ActiveShot,
	// AND Sequencer binding overrides may resolve to a different Actor than
	// ActiveShot.Target.Actor (which is often None for binding-driven Shot
	// Sections) — so a seed-once-on-host-change pass against ActiveShot would
	// silently no-op for the override case. Refreshing per-tick on the
	// EffectiveShot copy mirrors `EBoundsCachePolicy::Live` semantics and
	// trades a `GetComponentsBoundingBox` call per AutoFromComponentBounds
	// target per tick (O(actor component count), single-actor scope) for
	// "bounds-fit FOV actually works in the editor preview". The cache write
	// lives on the discarded local; ActiveShot is not touched.
	for (FComposableCameraShotTarget& T : EffectiveShot.Targets)
	{
		if (T.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds)
		{
			T.RefreshAutoBoundsCache();
		}
	}

	const FShotSolveResult Result = SolveShot(EffectiveShot, Context);

	// LENS (FOV) — always applied, regardless of `Result.bValid` AND
	// regardless of editor mode. The solver pre-fills `R.FieldOfView`
	// before the pose-failure exits (Manual mode → ManualFOV exactly;
	// SolvedFromBoundsFit on pose-fail → PreviousFrameFOV best-effort),
	// so this assignment is meaningful even when the joint Picard fails
	// to converge or an anchor can't resolve. Without this, dragging the
	// Manual FOV slider (or any Details-panel value) is invisible to the
	// designer whenever the Shot is in an unsolvable state — they have
	// no way to escape because the slider that would fix it appears
	// frozen.
	ViewFOV = Result.FieldOfView;
	ControllingActorViewInfo.FOV = Result.FieldOfView;

	if (!Result.bValid)
	{
		// Pose unresolvable — leave camera where it was so the designer
		// can still see whatever proxies are spawned. Lens (above) was
		// already applied so Details-panel slider drags stay live and
		// the designer can drag toward a solvable configuration. Skip
		// the DoF push (focus distance from a stale pose would be wrong)
		// and the pose write.
		bCachedDoFValid = false;
		return;
	}

	// POSE (location / rotation) — only in Drag and Lock. In Free, the
	// camera location/rotation come from the user's mouse drags through
	// the base FEditorViewportClient input handling; we don't overwrite.
	if (CurrentMode != EShotEditorMode::Free)
	{
		SetViewLocation(Result.CameraPosition);
		SetViewRotation(Result.CameraRotation);
	}
	else
	{
		// Free mode Roll is owned by `Shot.Roll` (authored via
		// Alt+RMB-drag inside a transaction so Ctrl+Z restores it).
		// Base `FEditorViewportClient` only writes Yaw/Pitch during
		// RMB-look — view-rotation Roll is unmanaged by base, so
		// re-asserting it from `Shot.Roll` each tick gives:
		//
		//   - Immediate visual feedback during the drag (Roll cursor
		//     samples write `Shot.Roll`; this sync mirrors them onto
		//     view rotation on the next tick).
		//   - Undo recovery: on Ctrl+Z, `Shot.Roll` reverts via the
		//     transaction; the next tick syncs view-rotation Roll to
		//     match. Without this, view-rotation would stay at the
		//     post-drag value and Ctrl+Z would silently disagree with
		//     the data.
		//   - Mode-entry consistency: switching into Free mode while
		//     `Shot.Roll != 0` shows the authored Roll immediately
		//     instead of resetting to 0.
		FRotator FreeRot = GetViewRotation();
		const float TargetRoll = ActiveShot ? ActiveShot->Roll : 0.f;
		if (!FMath::IsNearlyEqual(FreeRot.Roll, TargetRoll, 1e-3f))
		{
			FreeRot.Roll = TargetRoll;
			SetViewRotation(FreeRot);
		}
	}

	// FOCUS recomputation in Free mode: the solver's Focus pass reports
	// depth from the SOLVER's camera pose (which we just discarded in Free
	// mode). Recompute using the USER's actual camera pose so DoF focuses
	// on the right depth.
	float EffectiveFocusDistance = Result.FocusDistance;
	if (CurrentMode == EShotEditorMode::Free)
	{
		// SolveFocus is independent of placement / aim — feed it the user's
		// live camera pose and the existing Shot data so any FollowAnchor
		// mode (Placement / Aim / Custom) re-evaluates correctly.
		EffectiveFocusDistance = SolveFocus(
			*ActiveShot, GetViewLocation(), GetViewRotation());
	}

	// Cache for HUD readout.
	CachedFocusDistance = EffectiveFocusDistance;
	CachedAperture      = Result.Aperture;
	bCachedDoFValid     = true;

	// CineCamera-style DoF injection. Writes directly into
	// `ControllingActorViewInfo.PostProcessSettings` because that's the
	// route the engine reads when `bUseControllingActorViewInfo == true`
	// (see EditorViewportClient.cpp:1444-1448). The virtual
	// `OverridePostProcessSettings` hook is the OTHER branch (fires only
	// when bUseControllingActorViewInfo is false) and would never be
	// called in our setup.
	//
	// Mirrors the subset of fields `UCineCameraComponent::GetCameraView`
	// populates: FocalDistance + Fstop drive the bokeh circle radius (the
	// visible "out-of-focus" amount), SensorWidth scales the relationship
	// between f-stop and bokeh size (smaller sensor = less bokeh per f-stop).
	// We don't expose sensor width as a Shot parameter in V1 — defaulting
	// to 35mm full-frame (36mm horizontal) matches CineCamera's default
	// `Filmback.SensorWidth` so the preview matches a stock CineCamera
	// playback. Phase E may surface sensor width as a Shot UPROPERTY if
	// designers want to author for non-full-frame setups.
	FPostProcessSettings& PP = ControllingActorViewInfo.PostProcessSettings;

	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance           = FMath::Max(EffectiveFocusDistance, 1.f);

	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop           = FMath::Max(Result.Aperture, 0.5f);

	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth           = 36.f;   // 35mm full-frame
}

AActor* FComposableCameraShotEditorViewportClient::ResolveSourceActorForTargetIndex(int32 TargetIndex) const
{
	if (!ActiveShot || !ActiveShot->Targets.IsValidIndex(TargetIndex))
	{
		return nullptr;
	}

	// Per-section TargetActorOverrides take precedence over the Shot's
	// authored TSoftObjectPtr<AActor>. Only meaningful when the host is a
	// UMovieSceneComposableCameraShotSection — for ShotAsset / direct
	// CompositionFramingNode hosts, no override mechanism applies.
	if (UObject* Host = ActiveHost.Get())
	{
		if (UMovieSceneComposableCameraShotSection* Section = Cast<UMovieSceneComposableCameraShotSection>(Host))
		{
			for (const FComposableCameraShotTargetActorOverride& Override : Section->TargetActorOverrides)
			{
				if (Override.TargetIndex != TargetIndex || !Override.Binding.IsValid())
				{
					continue;
				}

				// Locate the open Sequencer for this section's owning
				// sequence. Without an open Sequencer there's no playback
				// context to resolve binding GUIDs against — fall through
				// to the Shot's authored placeholder. Same edge case as
				// "user closed the Sequencer window before opening the
				// Shot Editor".
				UMovieSceneSequence* Sequence = Section->GetTypedOuter<UMovieSceneSequence>();
				if (!Sequence)
				{
					continue;
				}

				FComposableCameraSystemEditorModule& EditorModule =
					FModuleManager::GetModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
				const TSharedPtr<ISequencer> OpenSequencer = EditorModule.FindOpenSequencerForSequence(Sequence);
				if (!OpenSequencer.IsValid())
				{
					continue;
				}

				// FindBoundObjects walks the sequence instance's binding map.
				// Possessables resolve immediately to their level-world actor;
				// Spawnables resolve only while their binding's section is
				// active in the timeline (so user must scrub the playhead
				// inside the spawnable's range to see it in the preview).
				// FocusedTemplateID handles sub-sequences correctly (binding
				// GUIDs are namespaced per-sub-sequence).
				const TArrayView<TWeakObjectPtr<>> Bound =
					OpenSequencer->FindBoundObjects(Override.Binding.GetGuid(), OpenSequencer->GetFocusedTemplateID());
				for (TWeakObjectPtr<UObject> Weak : Bound)
				{
					if (AActor* Actor = Cast<AActor>(Weak.Get()))
					{
						return Actor;
					}
				}
				// Override entry exists but binding doesn't resolve this
				// frame — fall back to placeholder so the proxy doesn't
				// flicker between live actor and cylinder.
				break;
			}
		}
	}

	// Authored placeholder.
	return ActiveShot->Targets[TargetIndex].Target.Actor.Get();
}

bool FComposableCameraShotEditorViewportClient::BuildEffectiveShotForPreview(FComposableCameraShot& OutShot) const
{
	// Per-frame cache fast path (Polish P.2). When the cache is valid for
	// this tick, copy out from cache and skip the per-target Sequencer-
	// override resolution — the dominant cost on the prior path. The
	// struct copy itself still runs (caller owns OutShot and may mutate
	// it), but it's a memcpy-shape transfer plus one TArray heap alloc
	// for the Targets array, which is unavoidable given the existing
	// API contract.
	if (bEffectiveShotCacheValid)
	{
		if (bEffectiveShotCacheBuiltOk)
		{
			OutShot = CachedEffectiveShot;
			return true;
		}
		return false;
	}

	if (!ActiveShot)
	{
		bEffectiveShotCacheValid    = true;
		bEffectiveShotCacheBuiltOk  = false;
		return false;
	}

	// Build directly into the cache, then copy to OutShot. Two struct
	// copies on the cache-miss path (one into cache, one out to caller)
	// vs. one on the warm path — the miss happens at most once per tick
	// while the hit happens 5+ times, net win. Building straight into
	// cache (vs. caller-buffer-then-cache) is simpler and lets later
	// hits reuse without re-running ResolveSourceActorForTargetIndex.
	CachedEffectiveShot = *ActiveShot;
	for (int32 i = 0; i < CachedEffectiveShot.Targets.Num(); ++i)
	{
		// `ResolveSourceActorForTargetIndex` already encapsulates the
		// override → placeholder → null fallback chain. Assigning a raw
		// AActor* into the soft-pointer captures the actor's path so the
		// solver's downstream `.Get()` resolves to the same instance.
		// When the resolver returns nullptr we leave the field as-is from
		// the placeholder copy (which may also be null — solver will
		// UNRESOLVED gracefully in that case).
		if (AActor* Resolved = ResolveSourceActorForTargetIndex(i))
		{
			CachedEffectiveShot.Targets[i].Target.Actor = Resolved;
		}
	}

	bEffectiveShotCacheValid    = true;
	bEffectiveShotCacheBuiltOk  = true;
	OutShot = CachedEffectiveShot;
	return true;
}

AActor* FComposableCameraShotEditorViewportClient::SpawnProxyForActor(AActor* SourceActor)
{
	if (!PreviewScene)
	{
		return nullptr;
	}
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bNoFail = true;

	// 1. Skeletal mesh? Highest fidelity stand-in — copy the SK mesh asset
	//    PLUS the AnimBP class so the proxy plays whatever idle / locomotion
	//    animation the source actor has (e.g. ABP_Manny in the UE5 third-
	//    person template gives a clean default Idle pose instead of the
	//    SkeletalMesh's raw A-pose). `SetUpdateAnimationInEditor(true)` is
	//    REQUIRED — without it AnimBP doesn't tick in editor preview worlds.
	//
	//    Note: `SetLeaderPoseComponent` was tried as a way to mirror the
	//    SOURCE actor's live Sequencer-driven pose into the preview, but
	//    cross-world leader-follower (source in level / PIE world, follower
	//    in our AdvancedPreviewScene's world) doesn't propagate into the
	//    follower's render-scene registration — the follower stays
	//    invisible. Fall back to the proxy's own AnimBP tick (independent
	//    Idle / locomotion) until a per-bone copy alternative is wired up.
	if (SourceActor)
	{
		if (USkeletalMeshComponent* SourceSK = SourceActor->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (USkeletalMesh* Mesh = SourceSK->GetSkeletalMeshAsset())
			{
				ASkeletalMeshActor* Proxy = PreviewWorld->SpawnActor<ASkeletalMeshActor>(Params);
				if (Proxy && Proxy->GetSkeletalMeshComponent())
				{
					USkeletalMeshComponent* ProxySK = Proxy->GetSkeletalMeshComponent();

					// CRITICAL: place the actor at the source's MESH world
					// transform FIRST, before any SetSkeletalMeshAsset / anim
					// init / bounds work. Doing it in the other order leaves
					// the SceneProxy initialized against world-origin bounds;
					// the subsequent transform change marks bounds dirty but
					// the cached SceneProxy in the FScene's static array is
					// already established and frustum culling against camera
					// (155, 619, 164) misses the world-origin sphere → mesh
					// is silently culled and never drawn. Transform-first
					// keeps bounds and SceneProxy aligned from the start.
					//
					// Why source MESH transform, not source ACTOR transform:
					// `ACharacter` defaults Mesh component to RelativeLocation
					// (0, 0, -88) so the mesh's feet align with the capsule's
					// bottom; `ASkeletalMeshActor` (our proxy class) has
					// SkeletalMeshComponent as its ROOT (no -88 offset). If
					// we used the source's actor transform, the proxy mesh
					// would render 88cm higher than the source mesh, and any
					// bone (including head) would project to a different
					// screen position than the solver expects.
					Proxy->SetActorTransform(SourceSK->GetComponentTransform());

					ProxySK->SetSkeletalMeshAsset(Mesh);

					// Disable BOTH the anim graph AND the component tick.
					// Notes after reading SkeletalMeshComponent.cpp ShouldTickPose:
					//   - SetUpdateAnimationInEditor(false) only short-
					//     circuits when WorldType==Editor. AdvancedPreviewScene
					//     uses WorldType::EditorPreview, so that flag is
					//     IGNORED and the AnimSingleNode AnimInstance ticks
					//     every frame, outputting ref pose which overwrites
					//     the CST we manually copy in SyncProxyTransforms.
					//   - SetComponentTickEnabled(false) is the unconditional
					//     gate — no component tick → no TickPose → no anim
					//     graph eval → no CST overwrite. The component still
					//     renders, transform changes propagate via
					//     SetActorTransform, and we drive the bones via
					//     ApplyEditedComponentSpaceTransforms each frame from
					//     our viewport's own Tick. No need for the proxy's
					//     own tick at all.
					ProxySK->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					ProxySK->SetComponentTickEnabled(false);
					Proxy->SetActorTickEnabled(false);
					ProxySK->UpdateBounds();
					ProxySK->MarkRenderTransformDirty();
					ProxySK->MarkRenderStateDirty();

					return Proxy;
				}
			}
		}

		// 2. Static mesh? Mid fidelity — copy the SM asset.
		if (UStaticMeshComponent* SourceSM = SourceActor->FindComponentByClass<UStaticMeshComponent>())
		{
			if (UStaticMesh* Mesh = SourceSM->GetStaticMesh())
			{
				AStaticMeshActor* Proxy = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
				if (Proxy && Proxy->GetStaticMeshComponent())
				{
					Proxy->GetStaticMeshComponent()->SetStaticMesh(Mesh);
					// Same alignment principle as the SkelMesh path: match
					// the source's MESH COMPONENT world transform so any
					// bone-equivalent reference (e.g. a static-mesh socket)
					// projects faithfully under the solver-driven camera.
					Proxy->SetActorTransform(SourceSM->GetComponentTransform());
					return Proxy;
				}
			}
		}
	}

	// 3. Fallback: capsule-ish cylinder at the source's transform (or origin).
	UStaticMesh* CapsuleMesh = LoadObject<UStaticMesh>(
		nullptr, kCapsuleFallbackMeshPath);
	AStaticMeshActor* Proxy = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
	if (Proxy && Proxy->GetStaticMeshComponent())
	{
		if (CapsuleMesh)
		{
			Proxy->GetStaticMeshComponent()->SetStaticMesh(CapsuleMesh);
		}
		Proxy->SetActorScale3D(kCapsuleFallbackScale);
		if (SourceActor)
		{
			Proxy->SetActorLocationAndRotation(
				SourceActor->GetActorLocation(), SourceActor->GetActorRotation());
		}
	}
	return Proxy;
}
